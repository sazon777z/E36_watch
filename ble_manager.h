#ifndef BLE_MANAGER_H
#define BLE_MANAGER_H

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include "config.h"

class BleManager {
public:
    BleManager();

    void init();
    void update();

    bool isConnected() const { return deviceConnected; }
    const char* getDeviceName() const { return BLE_DEVICE_NAME; }
    String getMacAddress() const { return macAddress; }

    void sendString(const String& str);
    void processCommand(const String& cmd);

    // Внутренние методы обратного вызова
    void setConnected(bool connected) { deviceConnected = connected; }

private:
    BLEServer* pServer;
    BLECharacteristic* pTxCharacteristic;
    BLECharacteristic* pRxCharacteristic;

    bool deviceConnected;
    bool oldDeviceConnected;
    String macAddress;
    unsigned long lastTelemetryBroadcast;
};

extern BleManager BleMgr;

#endif // BLE_MANAGER_H
