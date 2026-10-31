#include "ble_manager.h"
#include "time_keeper.h"
#include "display_driver.h"
#include "sensors.h"
#include "ui_engine.h"
#include "gps_driver.h"
#include "trip_computer.h"
#include "warning_manager.h"
#include "ota_manager.h"
#include "power_manager.h"

BleManager BleMgr;

class ServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
        BleMgr.setMtu(23); // До согласования MTU действует минимальный размер пакета
        BleMgr.setConnected(true);
        Serial.println("[BLE] Клиент подключился.");
    }

    void onDisconnect(BLEServer* pServer) {
        BleMgr.setConnected(false);
        Ota.onDisconnect();
        Serial.println("[BLE] Клиент отключился.");
    }

#if defined(CONFIG_NIMBLE_ENABLED)
    void onMtuChanged(BLEServer* pServer, ble_gap_conn_desc* desc, uint16_t mtu) {
        BleMgr.setMtu(mtu);
        Serial.printf("[BLE] Согласован MTU: %u\n", mtu);
    }
#else
    void onMtuChanged(BLEServer* pServer, esp_ble_gatts_cb_param_t* param) {
        BleMgr.setMtu(param->mtu.mtu);
        Serial.printf("[BLE] Согласован MTU: %u\n", param->mtu.mtu);
    }
#endif
};

class RxCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
        // Копируем команду в очередь фиксированного размера (без выделения памяти в задаче BLE)
        BleMgr.queueCommand(pCharacteristic->getData(), pCharacteristic->getLength());
    }
};

BleManager::BleManager()
    : pServer(nullptr),
      pTxCharacteristic(nullptr),
      pRxCharacteristic(nullptr),
      deviceConnected(false),
      oldDeviceConnected(false),
      negotiatedMtu(23),
      advRestartAtMs(0),
      macAddress(""),
      lastTelemetryBroadcast(0),
      cmdQueue(nullptr) {
}

void BleManager::queueCommand(const uint8_t* data, size_t len) {
    if (!cmdQueue || !data || len == 0) return;
    BleCommand cmd;
    if (len >= BLE_CMD_MAX_LEN) len = BLE_CMD_MAX_LEN - 1;
    memcpy(cmd.text, data, len);
    cmd.text[len] = '\0';
    // Не ждем: если очередь переполнена, команда отбрасывается (защита от флуда)
    if (xQueueSend(cmdQueue, &cmd, 0) != pdTRUE) {
        Serial.println("[BLE WARN] Очередь команд переполнена, команда отброшена.");
    }
}

uint32_t BleManager::secureWriteFlags() {
#if BLE_PAIRING_ENABLED && defined(CONFIG_NIMBLE_ENABLED)
    // NimBLE: запись только по зашифрованному соединению после сопряжения с PIN (MITM)
    return BLECharacteristic::PROPERTY_WRITE_ENC | BLECharacteristic::PROPERTY_WRITE_AUTHEN;
#else
    return 0;
#endif
}

void BleManager::addNotifyDescriptor(BLECharacteristic* c) {
#if defined(CONFIG_BLUEDROID_ENABLED)
    // Bluedroid требует ручного дескриптора 2902; NimBLE добавляет его сам
    BLE2902* cccd = new BLE2902();
    c->addDescriptor(cccd);
#if BLE_PAIRING_ENABLED
    cccd->setAccessPermissions(ESP_GATT_PERM_READ_ENC_MITM | ESP_GATT_PERM_WRITE_ENC_MITM);
#endif
#else
    (void)c;
#endif
}

void BleManager::protectCharacteristic(BLECharacteristic* c) {
#if BLE_PAIRING_ENABLED && defined(CONFIG_BLUEDROID_ENABLED)
    c->setAccessPermissions(ESP_GATT_PERM_READ_ENC_MITM | ESP_GATT_PERM_WRITE_ENC_MITM);
#else
    (void)c;
#endif
}

void BleManager::init() {
    Serial.printf("[BLE] Инициализация BLE: %s\n", BLE_DEVICE_NAME);
    cmdQueue = xQueueCreate(BLE_CMD_QUEUE_LEN, sizeof(BleCommand));

    BLEDevice::init(BLE_DEVICE_NAME);

    // Запрашиваем увеличенный MTU для длинных JSON пакетов (фактический согласует телефон)
    BLEDevice::setMTU(512);

#if BLE_PAIRING_ENABLED
    // Сопряжение со статическим PIN: при первом подключении телефон попросит ввести BLE_PASSKEY.
    // Ключи сохраняются (bonding), повторно PIN не спрашивается.
    BLESecurity::setPassKey(true, BLE_PASSKEY);
    BLESecurity::setCapability(ESP_IO_CAP_OUT);
    BLESecurity::setAuthenticationMode(true, true, true);
    // Требовать сопряжение сразу при подключении (защищает и чтение телеметрии)
    BLESecurity::setForceAuthentication(true);
#endif

    // Получаем MAC-адрес
    macAddress = BLEDevice::getAddress().toString().c_str();
    macAddress.toUpperCase();

    // Создаем BLE сервер
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new ServerCallbacks());

    // Создаем сервис Nordic UART (NUS)
    BLEService *pService = pServer->createService(BLE_SERVICE_UUID);

    // TX характеристика (отправка данных на телефон/нотификации)
    pTxCharacteristic = pService->createCharacteristic(
        BLE_CHAR_TX_UUID,
        BLECharacteristic::PROPERTY_NOTIFY
    );
    addNotifyDescriptor(pTxCharacteristic);
    protectCharacteristic(pTxCharacteristic);

    // RX характеристика (прием команд от телефона)
    pRxCharacteristic = pService->createCharacteristic(
        BLE_CHAR_RX_UUID,
        BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR | secureWriteFlags()
    );
    pRxCharacteristic->setCallbacks(new RxCallbacks());
    protectCharacteristic(pRxCharacteristic);

    // Запуск сервиса
    pService->start();

    // Сервис беспроводного обновления прошивки
    Ota.init(pServer);

    // Запуск видимости (Advertising)
    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(BLE_SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x06); // функции для помощи в подключении к iPhone
    pAdvertising->setMinPreferred(0x12);
    BLEDevice::startAdvertising();

    Serial.printf("[BLE OK] Имя: %s | MAC: %s | Сопряжение: %s | Ожидание подключения...\n",
                  BLE_DEVICE_NAME, macAddress.c_str(), BLE_PAIRING_ENABLED ? "PIN" : "ВЫКЛ");
}

void BleManager::update() {
    // Безопасная обработка отложенных BLE команд в основном потоке (исключает сбои графики LVGL)
    if (cmdQueue) {
        BleCommand cmd;
        while (xQueueReceive(cmdQueue, &cmd, 0) == pdTRUE) {
            processCommand(String(cmd.text));
        }
    }

    // Обработка переподключения при разрыве связи (пауза 200 мс без блокировки loop)
    if (!deviceConnected && oldDeviceConnected) {
        oldDeviceConnected = false;
        advRestartAtMs = millis() + 200;
    }
    if (advRestartAtMs != 0 && (long)(millis() - advRestartAtMs) >= 0) {
        advRestartAtMs = 0;
        if (!deviceConnected) {
            pServer->startAdvertising();
            Serial.println("[BLE] Перезапуск видимости (Advertising)...");
        }
    }

    if (deviceConnected && !oldDeviceConnected) {
        oldDeviceConnected = true;
    }

    Ota.update();

    // Периодическая отправка телеметрии подключенному клиенту (каждые 500 мс)
    // Во время обновления прошивки телеметрия не отправляется, чтобы не занимать радиоканал
    if (deviceConnected && !Ota.isActive() && (millis() - lastTelemetryBroadcast >= 500)) {
        lastTelemetryBroadcast = millis();
        const SensorData& s = Sensors.getData();
        const TripData& tr = Trip.getData();
        const TripPeaks& pk = Trip.getPeaks();
        const WarningState& ws = Warnings.getState();

        // При отсутствии связи с MS2 параметры мотора передаются нулями с флагом ms2:0,
        // приложение в этом случае показывает прочерки вместо устаревших значений
        const bool on = s.ms2Online;
        char jsonBuf[360];
        snprintf(jsonBuf, sizeof(jsonBuf),
                 "{\"ms2\":%d,\"rpm\":%d,\"clt\":%.1f,\"boost\":%.2f,\"afr\":%.1f,\"volt\":%.1f,\"spd\":%.1f,\"trip\":%.1f,\"fuel\":%.1f,\"tz\":%d,\"peaks\":{\"rpm\":%d,\"b\":%.2f,\"c\":%.1f,\"a\":%.1f,\"spd\":%.1f,\"f\":%.1f},\"warn\":{\"b\":%d,\"c\":%d,\"a\":%d}}\n",
                 on ? 1 : 0,
                 on ? s.ms2.rpm : 0, on ? s.ms2.clt_c : 0.0f, on ? s.ms2.boost_bar : 0.0f, on ? s.ms2.afr : 0.0f,
                 on ? s.batteryVoltage : 0.0f,
                 tr.current_speed_kmh, tr.trip_distance_km, tr.instant_consumption, Time.getTimezoneOffset(),
                 pk.max_rpm, pk.peak_boost_bar, pk.max_clt_c, (pk.min_afr <= 30.0f) ? pk.min_afr : 0.0f, pk.max_speed_kmh, pk.peak_instant_fuel,
                 ws.boostAlarm ? 1 : 0, ws.cltAlarm ? 1 : 0, ws.afrAlarm ? 1 : 0);

        sendString(jsonBuf);
    }
}

void BleManager::sendString(const String& str) {
    if (!deviceConnected || !pTxCharacteristic) return;

    // Одно уведомление вмещает не больше (MTU - 3) байт. Длинные строки режем на части,
    // приложение склеивает их обратно до символа перевода строки.
    size_t chunk = (negotiatedMtu > 3) ? (size_t)(negotiatedMtu - 3) : 20;
    if (chunk > 509) chunk = 509;

    const uint8_t* p = (const uint8_t*)str.c_str();
    size_t left = str.length();
    while (left > 0 && deviceConnected) {
        size_t n = (left < chunk) ? left : chunk;
        pTxCharacteristic->setValue((uint8_t*)p, n);
        pTxCharacteristic->notify();
        p += n;
        left -= n;
        if (left > 0) delay(3); // даем стеку отправить пакет, чтобы не переполнить очередь
    }
}

static bool parseFloatArg(const String& s, float& out) {
    String t = s;
    t.trim();
    if (t.length() == 0) return false;
    char* end = nullptr;
    out = strtof(t.c_str(), &end);
    return end != t.c_str();
}

void BleManager::processCommand(const String& cmd) {
    String c = cmd;
    c.trim();
    if (c.length() == 0) return;
    Power.notifyActivity();

    if (c.startsWith("tz ") || c.startsWith("timezone ")) {
        int tz = c.substring(c.indexOf(' ') + 1).toInt();
        if (tz >= -12 && tz <= 14) {
            Time.setTimezoneOffset((int8_t)tz);
            char reply[32];
            snprintf(reply, sizeof(reply), "OK: TZ %d\n", tz);
            sendString(reply);
            Serial.printf("[BLE] Установлен часовой пояс: UTC%+d\n", tz);
        } else {
            sendString("ERR: INVALID TZ (-12..+14)\n");
        }
    }
    else if (c.equalsIgnoreCase("gettz")) {
        char reply[32];
        snprintf(reply, sizeof(reply), "{\"tz\":%d}\n", Time.getTimezoneOffset());
        sendString(reply);
    }
    else if (c.startsWith("epoch ")) {
        // Установка времени по Unix timestamp в секундах (диапазон 2020..2099)
        String valStr = c.substring(6);
        valStr.trim();
        time_t epoch = (time_t)strtoul(valStr.c_str(), NULL, 10);
        if (epoch >= 1577836800 && epoch < 4102444800LL) {
            Time.setEpoch(epoch);
            sendString("OK: TIME SYNCED\n");
            Serial.printf("[BLE] Время синхронизировано по epoch: %ld\n", (long)epoch);
        } else {
            sendString("ERR: INVALID EPOCH\n");
        }
    }
    else if (c.startsWith("time ")) {
        // Формат: time HH:MM:SS или time HH:MM
        int h = -1, m = -1, s = 0;
        int parsed = sscanf(c.c_str() + 5, "%d:%d:%d", &h, &m, &s);
        if (parsed >= 2 && h >= 0 && h <= 23 && m >= 0 && m <= 59 && s >= 0 && s <= 59) {
            Time.setTimeOnly(h, m, s);
            sendString("OK: TIME SET\n");
            Serial.printf("[BLE] Установлено время: %02d:%02d:%02d\n", h, m, s);
        } else {
            sendString("ERR: FORMAT HH:MM:SS (00:00:00..23:59:59)\n");
        }
    }
    else if (c.startsWith("date ")) {
        // Формат: date DD.MM.YYYY
        int d = 0, m = 0, y = 0;
        int parsed = sscanf(c.c_str() + 5, "%d.%d.%d", &d, &m, &y);
        static const uint8_t dim[12] = {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        bool ok = (parsed == 3 && y >= 2020 && y <= 2099 && m >= 1 && m <= 12 && d >= 1 && d <= dim[m - 1]);
        if (ok && m == 2 && d == 29) {
            ok = (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
        }
        if (ok) {
            Time.setDateOnly(d, m, y);
            sendString("OK: DATE SET\n");
            Serial.printf("[BLE] Установлена дата: %02d.%02d.%04d\n", d, m, y);
        } else {
            sendString("ERR: FORMAT DD.MM.YYYY\n");
        }
    }
    else if (c.startsWith("bright ")) {
        // Регулировка яркости аппаратно не поддерживается: подсветка всегда 100%
        sendString("OK: BRIGHTNESS FIXED 255\n");
    }
    else if (c.startsWith("screen ")) {
        // Переключение экрана: screen 1..6
        int scr = c.substring(7).toInt();
        if (scr >= 1 && scr <= 6) {
            UI.setScreen((ScreenId)scr);
            sendString("OK: SCREEN CHANGED\n");
        } else {
            sendString("ERR: SCREEN 1-6\n");
        }
    }
    else if (c.startsWith("gauge ")) {
        // Выбор датчика кругового прибора: gauge <boost|clt|afr|fuel|speed|iat|rpm>
        String g = c.substring(6);
        g.trim();
        g.toLowerCase();
        GaugeType gt = GaugeType::BOOST;
        bool ok = true;
        if (g == "boost" || g == "0") gt = GaugeType::BOOST;
        else if (g == "clt" || g == "coolant" || g == "1") gt = GaugeType::COOLANT;
        else if (g == "afr" || g == "2") gt = GaugeType::AFR;
        else if (g == "fuel" || g == "inst_fuel" || g == "3") gt = GaugeType::INST_FUEL;
        else if (g == "speed" || g == "spd" || g == "4") gt = GaugeType::SPEED;
        else if (g == "iat" || g == "intake" || g == "mat" || g == "5") gt = GaugeType::INTAKE_TEMP;
        else if (g == "rpm" || g == "tacho" || g == "6") gt = GaugeType::RPM;
        else ok = false;

        if (ok) {
            UI.setGaugeType(gt);
            UI.setScreen(ScreenId::SINGLE_GAUGE);
            sendString("OK: GAUGE CHANGED\n");
            Serial.printf("[BLE] Выбран датчик прибора: %d (%s)\n", (int)gt, g.c_str());
        } else {
            sendString("ERR: GAUGE [boost|clt|afr|fuel|speed|iat|rpm]\n");
        }
    }
    else if (c.equalsIgnoreCase("nextgauge")) {
        UI.nextGaugeType();
        UI.setScreen(ScreenId::SINGLE_GAUGE);
        sendString("OK: NEXT GAUGE\n");
    }
    else if (c.equalsIgnoreCase("resettrip")) {
        Trip.resetTrip();
        UI.notifyTripReset();
        sendString("OK: TRIP RESET\n");
    }
    else if (c.startsWith("setodo ")) {
        float km = 0.0f;
        if (parseFloatArg(c.substring(7), km) && km >= 0.0f && km <= 2000000.0f) {
            Trip.setTotalOdometer(km);
            sendString("OK: ODOMETER SET\n");
        } else {
            sendString("ERR: setodo <0..2000000>\n");
        }
    }
    else if (c.equalsIgnoreCase("getwarn")) {
        sendString(Warnings.getSettingsJson() + "\n");
    }
    else if (c.startsWith("setwarn ")) {
        String sub = c.substring(8);
        sub.trim();
        float val = 0.0f;
        bool ok = false;
        if (sub.startsWith("clt ")) {
            ok = parseFloatArg(sub.substring(4), val) && Warnings.setCltMax(val);
        } else if (sub.startsWith("boost ")) {
            ok = parseFloatArg(sub.substring(6), val) && Warnings.setBoostMax(val);
        } else if (sub.startsWith("afrmin ")) {
            ok = parseFloatArg(sub.substring(7), val) && Warnings.setAfrLimits(val, Warnings.getSettings().afr_lean_max);
        } else if (sub.startsWith("afrmax ")) {
            ok = parseFloatArg(sub.substring(7), val) && Warnings.setAfrLimits(Warnings.getSettings().afr_rich_min, val);
        } else if (sub.startsWith("en ")) {
            Warnings.setEnabled(sub.substring(3).toInt() != 0);
            ok = true;
        } else {
            sendString("ERR: setwarn [clt|boost|afrmin|afrmax|en] <val>\n");
            return;
        }
        Warnings.commit();
        sendString(ok ? "OK: " + Warnings.getSettingsJson() + "\n" : String("ERR: VALUE OUT OF RANGE\n"));
    }
    // Поддержка JSON пакетов от мобильного приложения (например, {"boost_max":1.3,"clt_max":102})
    else if (c.startsWith("{") && c.endsWith("}")) {
        bool found = false;
        bool allOk = true;
        float v = 0.0f;
        int idx = c.indexOf("\"boost_max\":");
        if (idx >= 0) {
            found = true;
            allOk &= parseFloatArg(c.substring(idx + 12), v) && Warnings.setBoostMax(v, false);
        }
        idx = c.indexOf("\"clt_max\":");
        if (idx >= 0) {
            found = true;
            allOk &= parseFloatArg(c.substring(idx + 10), v) && Warnings.setCltMax(v, false);
        }
        // AFR задается парой, чтобы промежуточная проверка min < max не отбрасывала значения
        float afrMin = Warnings.getSettings().afr_rich_min;
        float afrMax = Warnings.getSettings().afr_lean_max;
        bool afrFound = false;
        idx = c.indexOf("\"afr_min\":");
        if (idx >= 0) {
            afrFound = true;
            allOk &= parseFloatArg(c.substring(idx + 10), afrMin);
        }
        idx = c.indexOf("\"afr_max\":");
        if (idx >= 0) {
            afrFound = true;
            allOk &= parseFloatArg(c.substring(idx + 10), afrMax);
        }
        if (afrFound) {
            found = true;
            allOk &= Warnings.setAfrLimits(afrMin, afrMax, false);
        }
        idx = c.indexOf("\"warn_en\":");
        if (idx >= 0) {
            found = true;
            Warnings.setEnabled(c.substring(idx + 10).toInt() != 0, false);
        }

        // Одна запись во Flash на весь пакет
        Warnings.commit();

        if (!found) {
            sendString("ERR: INVALID JSON\n");
        } else if (!allOk) {
            sendString("ERR: VALUE OUT OF RANGE " + Warnings.getSettingsJson() + "\n");
        } else {
            sendString("OK: " + Warnings.getSettingsJson() + "\n");
        }
    }
    else if (c.equalsIgnoreCase("status")) {
        const SensorData& s = Sensors.getData();
        const TripData& tr = Trip.getData();
        char buf[200];
        if (s.ms2Online) {
            snprintf(buf, sizeof(buf),
                     "MS2: ONLINE | RPM: %d | Spd: %.0f | Trip: %.1f | Fuel: %.1f | CLT: %.0fC | Volt: %.1fV\n",
                     s.ms2.rpm, tr.current_speed_kmh, tr.trip_distance_km,
                     tr.instant_consumption, s.ms2.clt_c, s.batteryVoltage);
        } else {
            snprintf(buf, sizeof(buf),
                     "MS2: OFFLINE | Spd: %.0f | Trip: %.1f | FW: %s\n",
                     tr.current_speed_kmh, tr.trip_distance_km, FW_VERSION);
        }
        sendString(buf);
    }
    else if (c.equalsIgnoreCase("sleep")) {
        sendString("OK: SLEEP\n");
        delay(100);
        Power.enterDeepSleep();
    }
    else if (c.equalsIgnoreCase("help")) {
        sendString("CMDS: epoch, time, date, tz, screen (1..6), gauge [boost|clt|afr|fuel|speed|iat|rpm], nextgauge, resettrip, setodo, getwarn, setwarn [clt|boost|afrmin|afrmax|en] <val>, status, sleep\n");
    }
    else {
        sendString("ERR: UNKNOWN CMD (type 'help')\n");
    }
}
