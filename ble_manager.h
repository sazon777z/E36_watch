#ifndef BLE_MANAGER_H
#define BLE_MANAGER_H

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <BLESecurity.h>
#include <freertos/queue.h>
#include "config.h"

// Максимальная длина одной текстовой команды от телефона
#define BLE_CMD_MAX_LEN   160
#define BLE_CMD_QUEUE_LEN 8

struct BleCommand {
    char text[BLE_CMD_MAX_LEN];
};

class BleManager {
public:
    BleManager();

    void init();
    void update();

    bool isConnected() const { return deviceConnected; }
    const char* getDeviceName() const { return BLE_DEVICE_NAME; }
    String getMacAddress() const { return macAddress; }

    void sendString(const String& str);
    void queueCommand(const uint8_t* data, size_t len);
    void processCommand(const String& cmd);

    // Внутренние методы обратного вызова (вызываются из задачи BLE стека)
    void setConnected(bool connected) { deviceConnected = connected; }
    void setMtu(uint16_t mtu) { negotiatedMtu = mtu; }

    // Защита характеристик шифрованием (при включенном сопряжении), для NimBLE и Bluedroid
    static uint32_t secureWriteFlags();
    static void addNotifyDescriptor(BLECharacteristic* c);
    static void protectCharacteristic(BLECharacteristic* c);

private:
    BLEServer* pServer;
    BLECharacteristic* pTxCharacteristic;
    BLECharacteristic* pRxCharacteristic;

    volatile bool deviceConnected;
    bool oldDeviceConnected;
    volatile uint16_t negotiatedMtu;
    unsigned long advRestartAtMs;
    String macAddress;
    unsigned long lastTelemetryBroadcast;

    QueueHandle_t cmdQueue;
};

extern BleManager BleMgr;

#endif // BLE_MANAGER_H
