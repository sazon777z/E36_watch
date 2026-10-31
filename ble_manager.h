#ifndef BLE_MANAGER_H
#define BLE_MANAGER_H

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <BLESecurity.h>
#include "config.h"

// Bluetooth используется только для беспроводного обновления прошивки (см. ota_manager, ota.html)
class BleManager {
public:
    BleManager();

    void init();
    void update();

    bool isConnected() const { return deviceConnected; }
    const char* getDeviceName() const { return BLE_DEVICE_NAME; }
    String getMacAddress() const { return macAddress; }

    // Внутренние методы обратного вызова (вызываются из задачи BLE стека)
    void setConnected(bool connected) { deviceConnected = connected; }

    // Защита характеристик шифрованием (при включенном сопряжении), для NimBLE и Bluedroid
    static uint32_t secureWriteFlags();
    static void addNotifyDescriptor(BLECharacteristic* c);
    static void protectCharacteristic(BLECharacteristic* c);

private:
    BLEServer* pServer;

    volatile bool deviceConnected;
    bool oldDeviceConnected;
    unsigned long advRestartAtMs;
    String macAddress;
};

extern BleManager BleMgr;

#endif // BLE_MANAGER_H
