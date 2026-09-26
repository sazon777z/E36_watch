#include "ble_manager.h"
#include "time_keeper.h"
#include "display_driver.h"
#include "sensors.h"
#include "ui_engine.h"

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
        const TimeData& t = Time.getTime();

        char jsonBuf[128];
        snprintf(jsonBuf, sizeof(jsonBuf), 
                 "{\"rpm\":%d,\"clt\":%.1f,\"boost\":%.2f,\"afr\":%.1f,\"volt\":%.1f,\"time\":\"%02d:%02d:%02d\"}\n",
                 s.ms2.rpm, s.tempOutdoor, s.ms2.boost_bar, s.ms2.afr, s.batteryVoltage,
                 t.hour, t.minute, t.second);

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
        // Переключение экрана: screen 0..3
        int scr = c.substring(7).toInt();
        if (scr >= 0 && scr <= 3) {
            UI.setScreen((ScreenId)scr);
            sendString("OK: SCREEN CHANGED\n");
        } else {
            sendString("ERR: SCREEN 0-3\n");
        }
    }
    else if (c.equalsIgnoreCase("status")) {
        const SensorData& s = Sensors.getData();
        char buf[160];
        snprintf(buf, sizeof(buf), 
                 "MS2: %s | RPM: %d | Boost: %.2fb | CLT: %.0fC | AFR: %.1f | Volt: %.1fV\n",
                 s.ms2Online ? "ONLINE" : "OFFLINE",
                 s.ms2.rpm, s.ms2.boost_bar, s.tempOutdoor, s.ms2.afr, s.batteryVoltage);
        sendString(buf);
    }
    else if (c.equalsIgnoreCase("help")) {
        sendString("CMDS: epoch <sec>, time HH:MM[:SS], date DD.MM.YYYY, bright <0-255>, screen <0-3>, status\n");
    }
    else {
        sendString("ERR: UNKNOWN CMD (type 'help')\n");
    }
}
