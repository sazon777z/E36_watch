#include "ble_manager.h"
#include "ota_manager.h"

BleManager BleMgr;

class ServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
        BleMgr.setConnected(true);
        Serial.println("[BLE] Клиент подключился.");
    }

    void onDisconnect(BLEServer* pServer) {
        BleMgr.setConnected(false);
        Ota.onDisconnect();
        Serial.println("[BLE] Клиент отключился.");
    }
};

BleManager::BleManager()
    : pServer(nullptr),
      deviceConnected(false),
      oldDeviceConnected(false),
      advRestartAtMs(0),
      macAddress("") {
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
    BLEDevice::init(BLE_DEVICE_NAME);

    // Большой MTU ускоряет передачу прошивки (фактический согласует телефон)
    BLEDevice::setMTU(512);

#if BLE_PAIRING_ENABLED
    // Сопряжение со статическим PIN: при первом подключении телефон попросит ввести BLE_PASSKEY.
    // Ключи сохраняются (bonding), повторно PIN не спрашивается.
    BLESecurity::setPassKey(true, BLE_PASSKEY);
    BLESecurity::setCapability(ESP_IO_CAP_OUT);
    BLESecurity::setAuthenticationMode(true, true, true);
    BLESecurity::setForceAuthentication(true);
#endif

    macAddress = BLEDevice::getAddress().toString().c_str();
    macAddress.toUpperCase();

    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new ServerCallbacks());

    // Единственный сервис - обновление прошивки
    Ota.init(pServer);

    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(OTA_SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x06); // функции для помощи в подключении к iPhone
    pAdvertising->setMinPreferred(0x12);
    BLEDevice::startAdvertising();

    Serial.printf("[BLE OK] Имя: %s | MAC: %s | Сопряжение: %s | Только OTA\n",
                  BLE_DEVICE_NAME, macAddress.c_str(), BLE_PAIRING_ENABLED ? "PIN" : "ВЫКЛ");
}

void BleManager::update() {
    // Перезапуск видимости после отключения (пауза 200 мс без блокировки loop)
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
}
