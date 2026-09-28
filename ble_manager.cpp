#include "ble_manager.h"
#include "time_keeper.h"
#include "display_driver.h"
#include "sensors.h"
#include "ui_engine.h"
#include "gps_driver.h"
#include "trip_computer.h"
#include "warning_manager.h"

BleManager BleMgr;

class ServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
        BleMgr.setConnected(true);
        Serial.println("[BLE] Клиент подключился.");
    }

    void onDisconnect(BLEServer* pServer) {
        BleMgr.setConnected(false);
        Serial.println("[BLE] Клиент отключился.");
    }
};

class RxCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
        String rxValue = pCharacteristic->getValue().c_str();
        if (rxValue.length() > 0) {
            rxValue.trim();
            Serial.printf("[BLE RX] Получена команда: %s\n", rxValue.c_str());
            BleMgr.processCommand(rxValue);
        }
    }
};

BleManager::BleManager()
    : pServer(nullptr),
      pTxCharacteristic(nullptr),
      pRxCharacteristic(nullptr),
      deviceConnected(false),
      oldDeviceConnected(false),
      macAddress(""),
      lastTelemetryBroadcast(0) {}

void BleManager::init() {
    Serial.printf("[BLE] Инициализация BLE: %s\n", BLE_DEVICE_NAME);
    BLEDevice::init(BLE_DEVICE_NAME);

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
    pTxCharacteristic->addDescriptor(new BLE2902());

    // RX характеристика (прием команд от телефона)
    pRxCharacteristic = pService->createCharacteristic(
        BLE_CHAR_RX_UUID,
        BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR
    );
    pRxCharacteristic->setCallbacks(new RxCallbacks());

    // Запуск сервиса
    pService->start();

    // Запуск видимости (Advertising)
    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(BLE_SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x06); // функции для помощи в подключении к iPhone
    pAdvertising->setMinPreferred(0x12);
    BLEDevice::startAdvertising();

    Serial.printf("[BLE OK] Имя: %s | MAC: %s | Ожидание подключения...\n", 
                  BLE_DEVICE_NAME, macAddress.c_str());
}

void BleManager::update() {
    // Обработка переподключения при разрыве связи
    if (!deviceConnected && oldDeviceConnected) {
        delay(200); // пауза для стабилизации стека Bluetooth
        pServer->startAdvertising();
        Serial.println("[BLE] Перезапуск видимости (Advertising)...");
        oldDeviceConnected = deviceConnected;
    }

    if (deviceConnected && !oldDeviceConnected) {
        oldDeviceConnected = deviceConnected;
    }

    // Периодическая отправка телеметрии подключенному клиенту (каждые 500 мс)
    if (deviceConnected && (millis() - lastTelemetryBroadcast >= 500)) {
        lastTelemetryBroadcast = millis();
        const SensorData& s = Sensors.getData();
        const TripData& tr = Trip.getData();
        const WarningState& ws = Warnings.getState();

        char jsonBuf[220];
        snprintf(jsonBuf, sizeof(jsonBuf), 
                 "{\"rpm\":%d,\"clt\":%.1f,\"boost\":%.2f,\"afr\":%.1f,\"volt\":%.1f,\"spd\":%.1f,\"trip\":%.1f,\"fuel\":%.1f,\"warn\":{\"b\":%d,\"c\":%d,\"a\":%d}}\n",
                 s.ms2.rpm, s.ms2Online ? s.ms2.clt_c : s.tempOutdoor, s.ms2.boost_bar, s.ms2.afr, s.batteryVoltage,
                 tr.current_speed_kmh, tr.trip_distance_km, tr.instant_consumption,
                 ws.boostAlarm ? 1 : 0, ws.cltAlarm ? 1 : 0, ws.afrAlarm ? 1 : 0);

        sendString(jsonBuf);
    }
}

void BleManager::sendString(const String& str) {
    if (deviceConnected && pTxCharacteristic) {
        pTxCharacteristic->setValue((uint8_t*)str.c_str(), str.length());
        pTxCharacteristic->notify();
    }
}

void BleManager::processCommand(const String& cmd) {
    String c = cmd;
    c.trim();

    if (c.startsWith("epoch ")) {
        // Установка времени по Unix timestamp в секундах
        String valStr = c.substring(6);
        valStr.trim();
        time_t epoch = (time_t)strtoul(valStr.c_str(), NULL, 10);
        if (epoch > 100000) {
            Time.setEpoch(epoch);
            sendString("OK: TIME SYNCED\n");
            Serial.printf("[BLE] Время синхронизировано по epoch: %ld\n", epoch);
        } else {
            sendString("ERR: INVALID EPOCH\n");
        }
    } 
    else if (c.startsWith("time ")) {
        // Формат: time HH:MM:SS или time HH:MM
        int h = 0, m = 0, s = 0;
        int parsed = sscanf(c.c_str() + 5, "%d:%d:%d", &h, &m, &s);
        if (parsed >= 2) {
            Time.setTimeOnly(h, m, s);
            sendString("OK: TIME SET\n");
            Serial.printf("[BLE] Установлено время: %02d:%02d:%02d\n", h, m, s);
        } else {
            sendString("ERR: FORMAT HH:MM:SS\n");
        }
    }
    else if (c.startsWith("date ")) {
        // Формат: date DD.MM.YYYY
        int d = 1, m = 1, y = 2026;
        int parsed = sscanf(c.c_str() + 5, "%d.%d.%d", &d, &m, &y);
        if (parsed == 3) {
            Time.setDateOnly(d, m, y);
            sendString("OK: DATE SET\n");
            Serial.printf("[BLE] Установлена дата: %02d.%02d.%04d\n", d, m, y);
        } else {
            sendString("ERR: FORMAT DD.MM.YYYY\n");
        }
    }
    else if (c.startsWith("bright ")) {
        // Регулировка яркости экрана: bright 0..255
        int br = c.substring(7).toInt();
        br = constrain(br, 0, 255);
        Display.setBrightness((uint8_t)br);
        char buf[32];
        snprintf(buf, sizeof(buf), "OK: BRIGHTNESS %d\n", br);
        sendString(buf);
        Serial.printf("[BLE] Установлена яркость: %d\n", br);
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
        float km = c.substring(7).toFloat();
        if (km >= 0) {
            Trip.setTotalOdometer(km);
            sendString("OK: ODOMETER SET\n");
        }
    }
    else if (c.equalsIgnoreCase("getwarn")) {
        sendString(Warnings.getSettingsJson() + "\n");
    }
    else if (c.startsWith("setwarn ")) {
        String sub = c.substring(8);
        sub.trim();
        if (sub.startsWith("clt ")) {
            float val = sub.substring(4).toFloat();
            Warnings.setCltMax(val);
            sendString("OK: CLT WARN SET\n");
        } else if (sub.startsWith("boost ")) {
            float val = sub.substring(6).toFloat();
            Warnings.setBoostMax(val);
            sendString("OK: BOOST WARN SET\n");
        } else if (sub.startsWith("afrmin ")) {
            float val = sub.substring(7).toFloat();
            Warnings.setAfrLimits(val, Warnings.getSettings().afr_lean_max);
            sendString("OK: AFR MIN SET\n");
        } else if (sub.startsWith("afrmax ")) {
            float val = sub.substring(7).toFloat();
            Warnings.setAfrLimits(Warnings.getSettings().afr_rich_min, val);
            sendString("OK: AFR MAX SET\n");
        } else if (sub.startsWith("en ")) {
            int en = sub.substring(3).toInt();
            Warnings.setEnabled(en != 0);
            sendString("OK: WARN ENABLE SET\n");
        } else {
            sendString("ERR: setwarn [clt|boost|afrmin|afrmax|en] <val>\n");
        }
    }
    // Поддержка JSON пакетов от мобильного приложения (например, {"boost_max":1.3,"clt_max":102})
    else if (c.startsWith("{") && c.endsWith("}")) {
        bool updated = false;
        int idx = c.indexOf("\"boost_max\":");
        if (idx >= 0) {
            float v = c.substring(idx + 12).toFloat();
            Warnings.setBoostMax(v);
            updated = true;
        }
        idx = c.indexOf("\"clt_max\":");
        if (idx >= 0) {
            float v = c.substring(idx + 10).toFloat();
            Warnings.setCltMax(v);
            updated = true;
        }
        idx = c.indexOf("\"afr_max\":");
        if (idx >= 0) {
            float v = c.substring(idx + 10).toFloat();
            Warnings.setAfrLimits(Warnings.getSettings().afr_rich_min, v);
            updated = true;
        }
        idx = c.indexOf("\"afr_min\":");
        if (idx >= 0) {
            float v = c.substring(idx + 10).toFloat();
            Warnings.setAfrLimits(v, Warnings.getSettings().afr_lean_max);
            updated = true;
        }
        idx = c.indexOf("\"warn_en\":");
        if (idx >= 0) {
            int en = c.substring(idx + 10).toInt();
            Warnings.setEnabled(en != 0);
            updated = true;
        }

        if (updated) {
            sendString("OK: " + Warnings.getSettingsJson() + "\n");
        } else {
            sendString("ERR: INVALID JSON\n");
        }
    }
    else if (c.equalsIgnoreCase("status")) {
        const SensorData& s = Sensors.getData();
        const TripData& tr = Trip.getData();
        char buf[192];
        snprintf(buf, sizeof(buf), 
                 "MS2: %s | RPM: %d | Spd: %.0f | Trip: %.1f | Fuel: %.1f | CLT: %.0fC | Volt: %.1fV\n",
                 s.ms2Online ? "ONLINE" : "OFFLINE",
                 s.ms2.rpm, tr.current_speed_kmh, tr.trip_distance_km,
                 tr.instant_consumption, s.tempOutdoor, s.batteryVoltage);
        sendString(buf);
    }
    else if (c.equalsIgnoreCase("help")) {
        sendString("CMDS: epoch, time, date, bright, screen (1..6), gauge [boost|clt|afr|fuel|speed|iat|rpm], nextgauge, resettrip, setodo, getwarn, setwarn [clt|boost|afrmin|afrmax|en] <val>, status\n");
    }
    else {
        sendString("ERR: UNKNOWN CMD (type 'help')\n");
    }
}
