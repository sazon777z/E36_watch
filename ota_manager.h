#ifndef OTA_MANAGER_H
#define OTA_MANAGER_H

#include <Arduino.h>
#include <BLEServer.h>
#include <BLE2902.h>
#include "config.h"

// Обновление прошивки по BLE.
// Протокол (характеристика CTRL, текстовые команды, ответы приходят notify):
//   begin <size> <crc32_hex> <pin>  -> "OK BEGIN" | "ERR ..."
//   (далее блоки прошивки пишутся в характеристику DATA с подтверждением, по порядку)
//   устройство периодически шлет "PROG <bytes>"
//   end                              -> "OK DONE" (перезагрузка через ~1.5 с) | "ERR ..."
//   abort                            -> "OK ABORT"
//   ver                              -> "VER <FW_VERSION> <running partition>"
class OtaManager {
public:
    OtaManager();

    void init(BLEServer* server);
    void update();

    bool isActive() const { return active; }
    void onDisconnect();

    // Вызываются из BLE стека
    void onCtrlWrite(const uint8_t* data, size_t len);
    void onDataWrite(const uint8_t* data, size_t len);

private:
    void reply(const char* msg);
    void fail(const char* msg);
    void handleBegin(const char* args);
    void handleEnd();

    BLECharacteristic* pCtrl;
    BLECharacteristic* pData;

    volatile bool active;
    volatile bool restartPending;
    volatile unsigned long lastActivityMs;
    unsigned long restartAtMs;
    bool appConfirmed;

    uint32_t expectedSize;
    uint32_t expectedCrc;
    uint32_t receivedBytes;
    uint32_t runningCrc;
    uint32_t lastProgressReport;
};

extern OtaManager Ota;

#endif // OTA_MANAGER_H
