#include "ts_bridge.h"
#include "can_driver.h"
#include "power_manager.h"
#include "soc/soc_caps.h"

TsBridgeClass TsBridge;

#if TS_BRIDGE_ENABLED && SOC_USB_OTG_SUPPORTED

#include <usb/usb_host.h>
#include <soc/usb_dwc_struct.h>
#include <freertos/stream_buffer.h>
#include <driver/gpio.h>
#include <esp_heap_caps.h>
#include "ts_link.h"

// -----------------------------------------------------------------------------
// FTDI FT232R: команды управления (vendor requests)
// -----------------------------------------------------------------------------
static const uint16_t FTDI_VID              = 0x0403;
static const uint8_t  FTDI_SIO_RESET        = 0;
static const uint8_t  FTDI_SIO_MODEM_CTRL   = 1;
static const uint8_t  FTDI_SIO_FLOW_CTRL    = 2;
static const uint8_t  FTDI_SIO_SET_BAUD     = 3;
static const uint8_t  FTDI_SIO_SET_DATA     = 4;
static const uint8_t  FTDI_SIO_SET_LATENCY  = 9;

static const size_t   USB_XFER_SIZE   = 512;   // буфер bulk IN/OUT
static const size_t   STREAM_SIZE     = 4096;  // очереди байт в обе стороны


namespace {
    usb_host_client_handle_t s_client = nullptr;
    TaskHandle_t s_hostTask = nullptr;
    TaskHandle_t s_clientTask = nullptr;
    TaskHandle_t s_devTask = nullptr;
    TaskHandle_t s_radioTask = nullptr;

    volatile uint8_t s_newDevAddr = 0;
    volatile bool s_evNew = false;    // клиент получил NEW_DEV
    volatile bool s_evGone = false;   // клиент получил DEV_GONE
    usb_device_handle_t s_dev = nullptr;
    usb_transfer_t* s_ctrlXfer = nullptr;
    usb_transfer_t* s_inXfer = nullptr;
    usb_transfer_t* s_outXfer = nullptr;
    SemaphoreHandle_t s_ctrlDone = nullptr;
    SemaphoreHandle_t s_outDone = nullptr;
    volatile bool s_inBusy = false;
    volatile bool s_outBusy = false;
    volatile bool s_ftdiReady = false;
    volatile bool s_closing = false;
    uint8_t  s_epIn = 0, s_epOut = 0;
    uint16_t s_mpsIn = 64;
    bool     s_claimed = false;

    StreamBufferHandle_t s_toEcu = nullptr;    // радио -> FT232 -> MS2
    StreamBufferHandle_t s_fromEcu = nullptr;  // MS2 -> FT232 -> радио

    tslink::RadioLink s_link;
    bool s_service = false;   // сервисный режим: без USB-хоста, с печатью состояния радио

    // Диагностика USB для экрана (монитор порта при работе моста недоступен):
    // на каком шаге не удалось открыть FT232 (0 — ошибок не было)
    enum : uint8_t {
        FAIL_NONE = 0, FAIL_OPEN, FAIL_DESC, FAIL_NOT_FTDI, FAIL_NO_EP,
        FAIL_CLAIM, FAIL_ALLOC, FAIL_SETUP, FAIL_SUBMIT
    };
    volatile uint8_t s_failStage = FAIL_NONE;
    volatile uint8_t s_enumSeen = 0;    // опознание дошло до дескриптора устройства (раз, до 9)
    volatile uint8_t s_newDevSeen = 0;  // событий NEW_DEV получено клиентом (до 9)
    uint8_t s_lastTriedAddr = 0;        // адрес, который уже пробовали открыть (опрос списка)

    // Вызывается при опознании, когда прочитан дескриптор устройства (задача usb_host)
    bool enumFilterCb(const usb_device_desc_t* dd, uint8_t* bConfigurationValue) {
        (void)dd;
        if (s_enumSeen < 9) s_enumSeen++;
        *bConfigurationValue = 1;
        return true;
    }

    uint32_t ftdiDivisor(uint32_t baud) {
        // FT232R/FT232BM: базовая частота 3 МГц, дробная часть в 1/8
        static const uint8_t divfrac[8] = {0, 3, 2, 4, 1, 5, 6, 7};
        uint32_t divisor3 = (48000000UL + baud) / (2UL * baud);  // = 24 МГц / baud, с округлением
        uint32_t divisor = divisor3 >> 3;
        divisor |= (uint32_t)divfrac[divisor3 & 0x7] << 14;
        if (divisor == 1) divisor = 0;
        else if (divisor == 0x4001) divisor = 1;
        return divisor;
    }

    // Очередь к ЭБУ наполняет задача Wi-Fi на другом ядре, поэтому не сбрасываем ее
    // (xStreamBufferReset), а безопасно вычитываем со стороны читателя
    void drainToEcu() {
        uint8_t tmp[64];
        while (xStreamBufferReceive(s_toEcu, tmp, sizeof(tmp), 0) > 0) {}
    }

    // ---------------- Колбэки USB (выполняются в задаче клиента) ----------------
    void ctrlCb(usb_transfer_t* t) {
        (void)t;
        xSemaphoreGive(s_ctrlDone);
    }

    void outCb(usb_transfer_t* t) {
        (void)t;
        s_outBusy = false;
        xSemaphoreGive(s_outDone);
    }

    void inCb(usb_transfer_t* t) {
        if (t->status == USB_TRANSFER_STATUS_COMPLETED) {
            const uint8_t* p = t->data_buffer;
            int n = t->actual_num_bytes;
            int mps = s_mpsIn ? s_mpsIn : 64;
            // Каждый пакет FTDI начинается с 2 байт состояния модема/линии — отбрасываем
            for (int i = 0; i < n; i += mps) {
                int c = (n - i) < mps ? (n - i) : mps;
                if (c > 2) xStreamBufferSend(s_fromEcu, p + i + 2, c - 2, 0);
            }
            if (s_ftdiReady && !s_closing && usb_host_transfer_submit(t) == ESP_OK) return;
        }
        s_inBusy = false;
    }

    void clientEventCb(const usb_host_client_event_msg_t* msg, void* arg) {
        (void)arg;
        // Флаги, а не уведомления задачи: уведомление devTask занято ее потоковым буфером
        // (xStreamBufferReceive ждет на нем же и сбрасывает его состояние)
        if (msg->event == USB_HOST_CLIENT_EVENT_NEW_DEV) {
            if (s_newDevSeen < 9) s_newDevSeen++;
            s_newDevAddr = msg->new_dev.address;
            s_evNew = true;
        } else if (msg->event == USB_HOST_CLIENT_EVENT_DEV_GONE) {
            s_evGone = true;
        }
    }

    // ---------------- Управление FT232 (из задачи устройства) ----------------
    bool ftdiCtrl(uint8_t req, uint16_t value, uint16_t index) {
        usb_setup_packet_t* s = (usb_setup_packet_t*)s_ctrlXfer->data_buffer;
        s->bmRequestType = USB_BM_REQUEST_TYPE_DIR_OUT | USB_BM_REQUEST_TYPE_TYPE_VENDOR |
                           USB_BM_REQUEST_TYPE_RECIP_DEVICE;
        s->bRequest = req;
        s->wValue = value;
        s->wIndex = index;
        s->wLength = 0;
        s_ctrlXfer->num_bytes = sizeof(usb_setup_packet_t);
        s_ctrlXfer->device_handle = s_dev;
        s_ctrlXfer->bEndpointAddress = 0;
        s_ctrlXfer->callback = ctrlCb;
        s_ctrlXfer->context = nullptr;
        s_ctrlXfer->timeout_ms = 1000;
        xSemaphoreTake(s_ctrlDone, 0);
        if (usb_host_transfer_submit_control(s_client, s_ctrlXfer) != ESP_OK) return false;
        if (xSemaphoreTake(s_ctrlDone, pdMS_TO_TICKS(1000)) != pdTRUE) return false;
        return s_ctrlXfer->status == USB_TRANSFER_STATUS_COMPLETED;
    }

    bool submitIn() {
        s_inXfer->device_handle = s_dev;
        s_inXfer->bEndpointAddress = s_epIn;
        s_inXfer->callback = inCb;
        s_inXfer->context = nullptr;
        s_inXfer->num_bytes = usb_round_up_to_mps(USB_XFER_SIZE, s_mpsIn);
        s_inBusy = true;
        if (usb_host_transfer_submit(s_inXfer) != ESP_OK) {
            s_inBusy = false;
            return false;
        }
        return true;
    }

    void freeTransfers() {
        if (s_ctrlXfer) { usb_host_transfer_free(s_ctrlXfer); s_ctrlXfer = nullptr; }
        if (s_inXfer)   { usb_host_transfer_free(s_inXfer);   s_inXfer = nullptr; }
        if (s_outXfer)  { usb_host_transfer_free(s_outXfer);  s_outXfer = nullptr; }
    }

    void closeDevice() {
        if (!s_dev) return;
        s_ftdiReady = false;
        s_closing = true;
        // Ждем завершения отправленных передач (при отключении они приходят со статусом NO_DEVICE)
        for (int i = 0; i < 100 && (s_inBusy || s_outBusy); i++) vTaskDelay(pdMS_TO_TICKS(10));
        if (s_inBusy && s_epIn) {
            usb_host_endpoint_halt(s_dev, s_epIn);
            usb_host_endpoint_flush(s_dev, s_epIn);
            usb_host_endpoint_clear(s_dev, s_epIn);
            for (int i = 0; i < 50 && s_inBusy; i++) vTaskDelay(pdMS_TO_TICKS(10));
        }
        if (s_claimed) {
            usb_host_interface_release(s_client, s_dev, 0);
            s_claimed = false;
        }
        freeTransfers();
        usb_host_device_close(s_client, s_dev);
        s_dev = nullptr;
        s_closing = false;
        drainToEcu();
        Serial.println("[TS] FT232 ЭБУ отключен.");
    }

    void openDevice(uint8_t addr) {
        if (s_dev || addr == 0) return;
        s_lastTriedAddr = addr;
        usb_device_handle_t dev = nullptr;
        if (usb_host_device_open(s_client, addr, &dev) != ESP_OK) { s_failStage = FAIL_OPEN; return; }

        const usb_device_desc_t* dd = nullptr;
        const usb_config_desc_t* cd = nullptr;
        if (usb_host_get_device_descriptor(dev, &dd) != ESP_OK || !dd ||
            usb_host_get_active_config_descriptor(dev, &cd) != ESP_OK || !cd) {
            s_failStage = FAIL_DESC;
            usb_host_device_close(s_client, dev);
            return;
        }
        if (dd->idVendor != FTDI_VID) {
            s_failStage = FAIL_NOT_FTDI;
            Serial.printf("[TS] USB-устройство %04X:%04X — не FTDI, пропускаю.\n", dd->idVendor, dd->idProduct);
            usb_host_device_close(s_client, dev);
            return;
        }

        int off = 0;
        const usb_intf_desc_t* intf = usb_parse_interface_descriptor(cd, 0, 0, &off);
        s_epIn = s_epOut = 0;
        if (intf) {
            for (int i = 0; i < intf->bNumEndpoints; i++) {
                int eo = off;
                const usb_ep_desc_t* ep = usb_parse_endpoint_descriptor_by_index(intf, i, cd->wTotalLength, &eo);
                if (!ep || (ep->bmAttributes & 0x03) != 0x02) continue;  // только bulk
                if (ep->bEndpointAddress & 0x80) {
                    s_epIn = ep->bEndpointAddress;
                    s_mpsIn = ep->wMaxPacketSize & 0x7FF;
                } else {
                    s_epOut = ep->bEndpointAddress;
                }
            }
        }
        if (!s_epIn || !s_epOut) {
            s_failStage = FAIL_NO_EP;
            usb_host_device_close(s_client, dev);
            return;
        }

        s_dev = dev;
        if (usb_host_interface_claim(s_client, s_dev, 0, 0) != ESP_OK) {
            s_failStage = FAIL_CLAIM;
            closeDevice();
            return;
        }
        s_claimed = true;
        if (usb_host_transfer_alloc(64, 0, &s_ctrlXfer) != ESP_OK ||
            usb_host_transfer_alloc(USB_XFER_SIZE, 0, &s_inXfer) != ESP_OK ||
            usb_host_transfer_alloc(USB_XFER_SIZE, 0, &s_outXfer) != ESP_OK) {
            s_failStage = FAIL_ALLOC;
            closeDevice();
            return;
        }

        uint32_t div = ftdiDivisor(TS_ECU_BAUD);
        bool ok = ftdiCtrl(FTDI_SIO_RESET, 0, 0) &&                        // сброс
                  ftdiCtrl(FTDI_SIO_RESET, 1, 0) &&                        // очистка RX
                  ftdiCtrl(FTDI_SIO_RESET, 2, 0) &&                        // очистка TX
                  ftdiCtrl(FTDI_SIO_SET_LATENCY, TS_FTDI_LATENCY_MS, 0) &&
                  ftdiCtrl(FTDI_SIO_SET_BAUD, div & 0xFFFF, div >> 16) &&
                  ftdiCtrl(FTDI_SIO_SET_DATA, 0x0008, 0) &&                // 8N1
                  ftdiCtrl(FTDI_SIO_FLOW_CTRL, 0, 0) &&                    // без потока
                  ftdiCtrl(FTDI_SIO_MODEM_CTRL, 0x0303, 0);                // DTR=1, RTS=1
        if (!ok) {
            Serial.println("[TS] Ошибка настройки FT232.");
            s_failStage = FAIL_SETUP;
            closeDevice();
            return;
        }

        drainToEcu();
        s_ftdiReady = true;
        if (!submitIn()) { s_failStage = FAIL_SUBMIT; closeDevice(); return; }
        s_failStage = FAIL_NONE;
        Serial.printf("[TS] FT232 ЭБУ подключен (PID %04X), %lu бод.\n",
                      dd->idProduct, (unsigned long)TS_ECU_BAUD);
    }

    // ---------------- Задачи FreeRTOS ----------------
    void hostLibTask(void*) {
        for (;;) {
            uint32_t flags = 0;
            usb_host_lib_handle_events(portMAX_DELAY, &flags);
            if (flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) usb_host_device_free_all();
        }
    }

    void clientTask(void*) {
        for (;;) usb_host_client_handle_events(s_client, portMAX_DELAY);
    }

    // Подключение/отключение FT232 и передача байт к ЭБУ
    void devTask(void*) {
        uint32_t lastInRetryMs = 0;
        uint32_t lastScanMs = 0;
        for (;;) {
            if (s_evGone) {
                s_evGone = false;
                closeDevice();
                s_lastTriedAddr = 0;
            }
            if (s_evNew) {
                s_evNew = false;
                openDevice(s_newDevAddr);
            }

            // Запасной путь: раз в секунду смотрим список опознанных устройств сами —
            // на случай, если событие NEW_DEV не пришло (или FT232 был подключен до старта)
            if (!s_dev && (uint32_t)(millis() - lastScanMs) >= 1000) {
                lastScanMs = millis();
                uint8_t addrs[4];
                int num = 0;
                if (usb_host_device_addr_list_fill(sizeof(addrs), addrs, &num) == ESP_OK) {
                    // Опробованное устройство отключили — его адрес может достаться новому
                    bool stillThere = false;
                    for (int i = 0; i < num && i < (int)sizeof(addrs); i++) {
                        if (addrs[i] == s_lastTriedAddr) stillThere = true;
                    }
                    if (!stillThere) s_lastTriedAddr = 0;
                    for (int i = 0; i < num && i < (int)sizeof(addrs) && !s_dev; i++) {
                        // Не-FTDI устройство не трогаем повторно; FT232 с ошибкой пробуем снова
                        if (addrs[i] != 0 && (addrs[i] != s_lastTriedAddr || s_failStage != FAIL_NOT_FTDI))
                            openDevice(addrs[i]);
                    }
                }
            }

            // Если прием остановился из-за ошибки, перезапускаем его
            if (s_ftdiReady && !s_inBusy && (uint32_t)(millis() - lastInRetryMs) > 100) {
                lastInRetryMs = millis();
                submitIn();
            }

            if (!s_ftdiReady || !s_outXfer) {
                vTaskDelay(pdMS_TO_TICKS(10));
                if (!s_ftdiReady) drainToEcu();
                continue;
            }

            size_t n = xStreamBufferReceive(s_toEcu, s_outXfer->data_buffer, USB_XFER_SIZE, pdMS_TO_TICKS(5));
            if (n == 0) continue;
            s_outXfer->device_handle = s_dev;
            s_outXfer->bEndpointAddress = s_epOut;
            s_outXfer->callback = outCb;
            s_outXfer->context = nullptr;
            s_outXfer->num_bytes = n;
            xSemaphoreTake(s_outDone, 0);
            s_outBusy = true;
            if (usb_host_transfer_submit(s_outXfer) != ESP_OK) {
                s_outBusy = false;
                continue;
            }
            xSemaphoreTake(s_outDone, pdMS_TO_TICKS(200));
        }
    }

    // Радио: HELLO, прием от ноутбука (колбэк) и отправка ответов ЭБУ
    void onRadioRx(const uint8_t* d, size_t n) {
        if (s_ftdiReady) xStreamBufferSend(s_toEcu, d, n, 0);
    }

    void radioTask(void*) {
        uint8_t buf[tslink::MAX_PAYLOAD];
        bool wasUp = false;
        uint32_t lastDiagMs = 0;
        for (;;) {
            s_link.setLocalFlags(s_ftdiReady ? tslink::FLAG_ECU_READY : 0);
            s_link.poll();
            bool up = s_link.linkUp();
            if (up != wasUp) {
                wasUp = up;
                Serial.printf("[TS] Связь с ноутбуком: %s\n", up ? "есть" : "нет");
            }
            if (s_service && (uint32_t)(millis() - lastDiagMs) >= 2000) {
                lastDiagMs = millis();
                Serial.printf("[TS] HELLO %lu (ошибок %lu), принято кадров %lu, сосед %s, связь %s; "
                              "память внутр. %u, PSRAM %u\n",
                              (unsigned long)s_link.statHelloTx, (unsigned long)s_link.statHelloErr,
                              (unsigned long)s_link.statRawRx, s_link.hasPeerNow() ? "да" : "нет",
                              up ? "есть" : "нет",
                              (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                              (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
            }
            size_t n = xStreamBufferReceive(s_fromEcu, buf, sizeof(buf), pdMS_TO_TICKS(10));
            if (n == 0) continue;
            if (up) s_link.send(buf, n);  // нет связи — ответ ЭБУ некому отдавать
        }
    }
}  // namespace

void TsBridgeClass::setVbus(bool on) {
#if PIN_USB_VBUS_EN >= 0
    if (on == vbusOn) return;
    digitalWrite(PIN_USB_VBUS_EN, on ? HIGH : LOW);
    vbusOn = on;
    Serial.printf("[TS] Питание FT232: %s\n", on ? "ВКЛ" : "выкл");
#else
    (void)on;
    vbusOn = true;
#endif
}

void TsBridgeClass::init() {
#if PIN_USB_VBUS_EN >= 0
    gpio_hold_dis((gpio_num_t)PIN_USB_VBUS_EN);
    pinMode(PIN_USB_VBUS_EN, OUTPUT);
    digitalWrite(PIN_USB_VBUS_EN, LOW);
    vbusOn = false;
#endif

    // Сервисный режим: кнопка удерживается при включении (не при пробуждении).
    // USB-хост не запускается: монитор порта и прошивка по USB работают. Радио работает,
    // и раз в 2 с в порт печатается его состояние (проверка связи с донглом на столе).
    if (!Power.wokeFromSleep() && digitalRead(PIN_BTN_NEXT) == LOW) {
        uint32_t t0 = millis();
        while (digitalRead(PIN_BTN_NEXT) == LOW && millis() - t0 < 1500) delay(10);
        if (millis() - t0 >= 1500) {
            serviceMode = true;
            s_service = true;
            Serial.println("[TS] Сервисный режим: USB-хост выключен, USB работает как монитор порта. Радио включено.");
        }
    }

    s_toEcu = xStreamBufferCreate(STREAM_SIZE, 1);
    s_fromEcu = xStreamBufferCreate(STREAM_SIZE, 1);
    s_ctrlDone = xSemaphoreCreateBinary();
    s_outDone = xSemaphoreCreateBinary();
    if (!s_toEcu || !s_fromEcu || !s_ctrlDone || !s_outDone) {
        Serial.println("[TS] Нет памяти для моста.");
        errStr = "NOMEM";
        return;
    }

    Serial.printf("[TS] Свободно до Wi-Fi: внутр. %u, PSRAM %u\n",
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    if (!s_link.begin(tslink::ROLE_CAR, TS_LINK_NET_ID, TS_LINK_CHANNEL,
                      TS_LINK_PMK, TS_LINK_LMK, onRadioRx, TS_LINK_TX_POWER)) {
        Serial.println("[TS] Ошибка запуска Wi-Fi / ESP-NOW.");
        WiFi.mode(WIFI_OFF);
        errStr = "ERR WIFI";
        return;
    }
    size_t freeInt = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    Serial.printf("[TS] Wi-Fi запущен, свободно внутр. %u\n", (unsigned)freeInt);
    if (freeInt < TS_MIN_FREE_INTERNAL) {
        // Без запаса ESP-NOW не выделит буферы пакетов и молча не выйдет в эфир
        Serial.println("[TS] Мало внутренней памяти: мост не запущен.");
        s_link.end();
        errStr = "NOMEM";
        return;
    }

    if (!serviceMode) {
        Serial.println("[TS] Включаю USB-хост. Монитор порта по USB дальше недоступен.");
        Serial.flush();
        delay(20);

        usb_host_config_t hc = {};
        hc.skip_phy_setup = false;
        hc.intr_flags = ESP_INTR_FLAG_LEVEL1;
#ifdef CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK
        hc.enum_filter_cb = enumFilterCb;   // только для диагностики (код f на экране)
#endif
        if (usb_host_install(&hc) != ESP_OK) {
            s_link.end();
            errStr = "ERR USB";
            return;
        }
        xTaskCreatePinnedToCore(hostLibTask, "usb_host", 3072, nullptr, 5, &s_hostTask, 0);

        usb_host_client_config_t cc = {};
        cc.is_synchronous = false;
        cc.max_num_event_msg = 5;
        cc.async.client_event_callback = clientEventCb;
        cc.async.callback_arg = nullptr;
        if (usb_host_client_register(&cc, &s_client) != ESP_OK) {
            s_link.end();
            errStr = "ERR USB";
            return;
        }

        xTaskCreatePinnedToCore(devTask, "ts_dev", 4096, nullptr, 4, &s_devTask, 0);
        xTaskCreatePinnedToCore(clientTask, "usb_client", 4096, nullptr, 5, &s_clientTask, 0);
    }
    xTaskCreatePinnedToCore(radioTask, "ts_radio", 4096, nullptr, 4, &s_radioTask, 0);

    running = true;
}

void TsBridgeClass::update() {
    if (!running) return;
    // FT232 под питанием только пока ЭБУ на связи по CAN
    setVbus(CanBus.isConnected());
}

void TsBridgeClass::shutdown() {
#if PIN_USB_VBUS_EN >= 0
    digitalWrite(PIN_USB_VBUS_EN, LOW);
    vbusOn = false;
    gpio_hold_en((gpio_num_t)PIN_USB_VBUS_EN);
#endif
}

bool TsBridgeClass::ecuReady() const { return running && s_ftdiReady; }
bool TsBridgeClass::linkUp() const { return running && s_link.linkUp(); }

const char* TsBridgeClass::statusStr() const {
    if (!running) return errStr ? errStr : "OFF";
    bool ecu = s_ftdiReady;
    bool pc = s_link.linkUp();
    const char* st = (ecu && pc) ? "LINK" : pc ? "PC" : ecu ? "ECU" : "WAIT";
    static char buf[24];
    if (serviceMode) {
        snprintf(buf, sizeof(buf), "SRV %s", st);
        return buf;
    }
    if (ecu) return st;
    // FT232 не открыт — показываем, что видит USB-хост:
    //   c0/c1 — есть ли подключенное устройство на линиях D+/D- (порт хоста);
    //   d<n>  — сколько устройств у стека USB (включая опознаваемое сейчас);
    //   f<n>  — сколько раз опознание дошло до дескриптора устройства;
    //   n<n>  — сколько событий «новое устройство» получил мост;
    //   e<n>  — шаг, на котором не открылся FT232 (0 — попыток с ошибкой не было)
    usb_host_lib_info_t info = {};
    usb_host_lib_info(&info);
    snprintf(buf, sizeof(buf), "%s c%u d%d f%u n%u e%u", st, (unsigned)USB_DWC.hprt_reg.prtconnsts,
             info.num_devices, (unsigned)s_enumSeen, (unsigned)s_newDevSeen, (unsigned)s_failStage);
    return buf;
}

#else  // мост выключен или чип без USB-хоста

void TsBridgeClass::init() {}
void TsBridgeClass::update() {}
void TsBridgeClass::shutdown() {}
void TsBridgeClass::setVbus(bool) {}
bool TsBridgeClass::ecuReady() const { return false; }
bool TsBridgeClass::linkUp() const { return false; }
const char* TsBridgeClass::statusStr() const { return "OFF"; }

#endif
