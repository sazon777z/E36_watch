// =============================================================================
// ts_link.h — радиоканал ESP-NOW «беспроводной кабель» TunerStudio <-> MegaSquirt 2
// -----------------------------------------------------------------------------
// ОДИН И ТОТ ЖЕ ФАЙЛ лежит в трех прошивках:
//   * дэшборд E36_watch (ESP32-S3, сторона машины)      -> E36_watch/ts_link.h
//   * донгл у ноутбука (ESP32-C3, сторона компьютера)   -> extras/ts_dongle_c3/ts_link.h
//   * диагностика донгла на столе (ESP32-C3)            -> extras/ts_dongle_diag/ts_link.h
// Меняете один — скопируйте его в остальные.
//
// Как работает:
//   * Оба устройства раз в 250–500 мс рассылают широковещательный HELLO с общим
//     номером пары (netId). Услышав друг друга, они добавляют соседа как
//     зашифрованного (LMK) адресата ESP-NOW. Сопряжения и подключения к сети нет.
//   * Данные (DATA) идут адресно, по одному пакету за раз: ждём подтверждения
//     доставки (MAC-ACK) и при неудаче повторяем. Номер пакета (seq) отсекает
//     повторы, поэтому байты не теряются, не дублируются и не меняют порядок.
//   * Сосед считается пропавшим, если 3 с от него нет ни HELLO, ни данных.
// =============================================================================
#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_random.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace tslink {

static const uint8_t  MAGIC           = 0xE3;
static const uint8_t  PROTO_VER       = 1;
static const uint8_t  PKT_HELLO       = 1;
static const uint8_t  PKT_DATA        = 2;
static const size_t   MAX_PAYLOAD     = 240;   // < ESP_NOW_MAX_DATA_LEN (250) с учетом заголовка
static const uint32_t LINK_TIMEOUT_MS = 3000;  // сосед молчит дольше -> связь потеряна
static const uint32_t HELLO_FAST_MS   = 250;   // HELLO, пока связи нет
static const uint32_t HELLO_SLOW_MS   = 500;   // HELLO при живой связи
static const int      SEND_ATTEMPTS   = 8;     // попыток доставки одного пакета
static const uint32_t SEND_WAIT_MS    = 30;    // ожидание подтверждения одной попытки

static const uint8_t ROLE_PC  = 1;  // донгл у ноутбука
static const uint8_t ROLE_CAR = 2;  // дэшборд в машине

static const uint8_t FLAG_ECU_READY = 0x01;  // сторона машины: FT232 ЭБУ подключен и настроен

struct __attribute__((packed)) HelloPkt {
    uint8_t  magic;
    uint8_t  ver;
    uint8_t  type;
    uint32_t netId;
    uint32_t bootId;      // случайное число при каждом старте: сосед видит перезагрузку
    uint8_t  role;
    uint8_t  peerMac[6];  // кого я сейчас считаю своим соседом (нули, если никого)
    uint8_t  flags;
};

struct __attribute__((packed)) DataHdr {
    uint8_t magic;
    uint8_t ver;
    uint8_t type;
    uint8_t seq;
    uint8_t len;
};

typedef void (*RxHandler)(const uint8_t* data, size_t len);

class RadioLink {
public:
    // role: ROLE_PC / ROLE_CAR. pmk16/lmk16: ровно 16 символов, одинаковые на обоих устройствах.
    // txPower: значение wifi_power_t (например WIFI_POWER_8_5dBm) или 0 — не менять.
    bool begin(uint8_t role, uint32_t netId, uint8_t channel,
               const char* pmk16, const char* lmk16, RxHandler onRx, int txPower = 0) {
        if (started) return true;
        self = this;
        myRole = role;
        myNetId = netId;
        rxHandler = onRx;
        memcpy(lmk, lmk16, 16);
        bootId = esp_random();
        sendDone = xSemaphoreCreateBinary();
        if (!sendDone) return false;

        if (!WiFi.mode(WIFI_STA)) return false;
        WiFi.disconnect();
        esp_wifi_set_ps(WIFI_PS_NONE);  // при работающем BLE может быть отклонено — не страшно
        esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
        if (txPower) WiFi.setTxPower((wifi_power_t)txPower);
        esp_wifi_get_mac(WIFI_IF_STA, myMac);

        if (esp_now_init() != ESP_OK) return false;
        esp_now_set_pmk((const uint8_t*)pmk16);
        esp_now_register_recv_cb(onRecvStatic);
        esp_now_register_send_cb(onSentStatic);

        esp_now_peer_info_t bc = {};
        memset(bc.peer_addr, 0xFF, 6);
        bc.channel = 0;
        bc.ifidx = WIFI_IF_STA;
        bc.encrypt = false;
        esp_now_add_peer(&bc);

        started = true;
        return true;
    }

    void end() {
        if (!started) return;
        started = false;
        esp_now_unregister_recv_cb();
        esp_now_unregister_send_cb();
        esp_now_deinit();
        WiFi.mode(WIFI_OFF);
        hasPeer = false;
        peerKnowsMe = false;
        pendingPeer = false;
    }

    // Вызывать часто (каждые 5–50 мс) из одной и той же задачи, что и send().
    void poll() {
        if (!started) return;
        uint32_t now = millis();

        // Добавление нового соседа (отложено из колбэка Wi-Fi в обычную задачу)
        if (pendingPeer && !hasPeer) {
            uint8_t mac[6];
            portENTER_CRITICAL(&mux);
            memcpy(mac, pendingMac, 6);
            pendingPeer = false;
            portEXIT_CRITICAL(&mux);
            esp_now_peer_info_t p = {};
            memcpy(p.peer_addr, mac, 6);
            p.channel = 0;
            p.ifidx = WIFI_IF_STA;
            p.encrypt = true;
            memcpy(p.lmk, lmk, 16);
            if (esp_now_is_peer_exist(mac)) esp_now_del_peer(mac);
            if (esp_now_add_peer(&p) == ESP_OK) {
                portENTER_CRITICAL(&mux);
                memcpy(peerMac, mac, 6);
                hasPeer = true;
                peerKnowsMe = false;
                rxSeqValid = false;
                lastHeardMs = now;
                portEXIT_CRITICAL(&mux);
            }
        }

        // Сосед пропал
        if (hasPeer && (uint32_t)(now - lastHeardMs) > LINK_TIMEOUT_MS) {
            esp_now_del_peer(peerMac);
            portENTER_CRITICAL(&mux);
            hasPeer = false;
            peerKnowsMe = false;
            peerReadyFlag = false;
            portEXIT_CRITICAL(&mux);
        }

        // Маячок HELLO
        uint32_t period = linkUp() ? HELLO_SLOW_MS : HELLO_FAST_MS;
        if ((uint32_t)(now - lastHelloMs) >= period) {
            lastHelloMs = now;
            HelloPkt h = {};
            h.magic = MAGIC;
            h.ver = PROTO_VER;
            h.type = PKT_HELLO;
            h.netId = myNetId;
            h.bootId = bootId;
            h.role = myRole;
            if (hasPeer) memcpy(h.peerMac, peerMac, 6);
            h.flags = localFlags;
            static const uint8_t bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
            if (esp_now_send(bcast, (const uint8_t*)&h, sizeof(h)) == ESP_OK) statHelloTx++;
            else statHelloErr++;  // чаще всего ESP_ERR_ESPNOW_NO_MEM: не хватает памяти
        }
    }

    // Надежная отправка (блокирует до подтверждения). false — связь потеряна или не доставлено.
    bool send(const uint8_t* data, size_t len) {
        while (len > 0) {
            if (!linkUp()) { statDrops++; return false; }
            size_t chunk = len > MAX_PAYLOAD ? MAX_PAYLOAD : len;
            uint8_t pkt[sizeof(DataHdr) + MAX_PAYLOAD];
            DataHdr* h = (DataHdr*)pkt;
            h->magic = MAGIC;
            h->ver = PROTO_VER;
            h->type = PKT_DATA;
            h->seq = txSeq;
            h->len = (uint8_t)chunk;
            memcpy(pkt + sizeof(DataHdr), data, chunk);

            bool ok = false;
            for (int attempt = 0; attempt < SEND_ATTEMPTS && !ok; ++attempt) {
                if (attempt) { statRetries++; vTaskDelay(1); }
                xSemaphoreTake(sendDone, 0);
                if (esp_now_send(peerMac, pkt, sizeof(DataHdr) + chunk) != ESP_OK) {
                    vTaskDelay(2);
                    continue;
                }
                if (xSemaphoreTake(sendDone, pdMS_TO_TICKS(SEND_WAIT_MS)) == pdTRUE && lastSendOk) ok = true;
            }
            txSeq++;
            if (!ok) { statDrops++; return false; }
            statTxPackets++;
            data += chunk;
            len -= chunk;
        }
        return true;
    }

    bool linkUp() const {
        return started && hasPeer && peerKnowsMe &&
               (uint32_t)(millis() - lastHeardMs) <= LINK_TIMEOUT_MS;
    }
    bool peerReady() const { return linkUp() && peerReadyFlag; }
    void setLocalFlags(uint8_t f) { localFlags = f; }
    bool isStarted() const { return started; }

    uint32_t statTxPackets = 0, statRxPackets = 0, statRetries = 0, statDrops = 0;
    uint32_t statHelloTx = 0, statHelloErr = 0;  // отправка HELLO: поставлено в очередь / ошибка
    volatile uint32_t statRawRx = 0;             // любые принятые кадры ESP-NOW
    bool hasPeerNow() const { return hasPeer; }

private:
    static RadioLink* self;

    bool started = false;
    uint8_t myRole = 0;
    uint32_t myNetId = 0;
    uint32_t bootId = 0;
    uint8_t myMac[6] = {0};
    uint8_t lmk[16] = {0};
    RxHandler rxHandler = nullptr;
    SemaphoreHandle_t sendDone = nullptr;
    portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

    volatile bool hasPeer = false;
    volatile bool peerKnowsMe = false;
    volatile bool peerReadyFlag = false;
    volatile bool pendingPeer = false;
    volatile bool lastSendOk = false;
    volatile bool rxSeqValid = false;
    volatile uint8_t lastRxSeq = 0;
    volatile uint32_t lastHeardMs = 0;
    uint32_t peerBootId = 0;
    uint8_t peerMac[6] = {0};
    uint8_t pendingMac[6] = {0};
    uint8_t txSeq = 0;
    uint8_t localFlags = 0;
    uint32_t lastHelloMs = 0;

    static void onSentStatic(const esp_now_send_info_t* info, esp_now_send_status_t status) {
        RadioLink* s = self;
        if (!s || !info || !info->des_addr) return;
        if (info->des_addr[0] == 0xFF && info->des_addr[1] == 0xFF) return;  // широковещательный HELLO
        if (memcmp(info->des_addr, s->peerMac, 6) != 0) return;
        s->lastSendOk = (status == ESP_NOW_SEND_SUCCESS);
        xSemaphoreGive(s->sendDone);
    }

    static void onRecvStatic(const esp_now_recv_info_t* info, const uint8_t* d, int n) {
        RadioLink* s = self;
        if (s) s->statRawRx++;
        if (!s || !info || n < 3 || d[0] != MAGIC || d[1] != PROTO_VER) return;
        if (d[2] == PKT_HELLO && n >= (int)sizeof(HelloPkt)) {
            s->handleHello(info->src_addr, (const HelloPkt*)d);
        } else if (d[2] == PKT_DATA && n >= (int)sizeof(DataHdr)) {
            s->handleData(info->src_addr, d, n);
        }
    }

    void handleHello(const uint8_t* src, const HelloPkt* h) {
        if (h->netId != myNetId || h->role == myRole || h->role == 0) return;
        portENTER_CRITICAL(&mux);
        if (!hasPeer) {
            if (!pendingPeer) {
                memcpy(pendingMac, src, 6);
                pendingPeer = true;
                peerBootId = h->bootId;
            }
        } else if (memcmp(src, peerMac, 6) == 0) {
            lastHeardMs = millis();
            if (h->bootId != peerBootId) {  // сосед перезагрузился
                peerBootId = h->bootId;
                rxSeqValid = false;
            }
            peerKnowsMe = (memcmp(h->peerMac, myMac, 6) == 0);
            peerReadyFlag = (h->flags & FLAG_ECU_READY) != 0;
        }
        portEXIT_CRITICAL(&mux);
    }

    void handleData(const uint8_t* src, const uint8_t* d, int n) {
        if (!hasPeer || memcmp(src, peerMac, 6) != 0) return;
        const DataHdr* h = (const DataHdr*)d;
        int len = h->len;
        if (len <= 0 || (int)sizeof(DataHdr) + len > n) return;
        bool dup;
        portENTER_CRITICAL(&mux);
        lastHeardMs = millis();
        dup = rxSeqValid && h->seq == lastRxSeq;
        lastRxSeq = h->seq;
        rxSeqValid = true;
        portEXIT_CRITICAL(&mux);
        if (dup) return;  // повтор: подтверждение до соседа не дошло, он отправил снова
        statRxPackets++;
        if (rxHandler) rxHandler(d + sizeof(DataHdr), (size_t)len);
    }
};

inline RadioLink* RadioLink::self = nullptr;

}  // namespace tslink
