#include "ota_manager.h"
#include "ble_manager.h"
#include <Update.h>
#include <esp_ota_ops.h>
#include <esp_rom_crc.h>

OtaManager Ota;

// Откладываем подтверждение новой прошивки: ядро Arduino не помечает образ рабочим при старте,
// это делает OtaManager::update() после OTA_CONFIRM_UPTIME_MS стабильной работы.
// Если прошивка упадет раньше, загрузчик откатится на предыдущую (при включенном APP_ROLLBACK).
extern "C" bool verifyRollbackLater() {
    return true;
}

class OtaCtrlCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* c) {
        Ota.onCtrlWrite(c->getData(), c->getLength());
    }
};

class OtaDataCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* c) {
        Ota.onDataWrite(c->getData(), c->getLength());
    }
};

OtaManager::OtaManager()
    : pCtrl(nullptr),
      pData(nullptr),
      active(false),
      restartPending(false),
      lastActivityMs(0),
      restartAtMs(0),
      appConfirmed(false),
      expectedSize(0),
      expectedCrc(0),
      receivedBytes(0),
      runningCrc(0),
      lastProgressReport(0) {
}

void OtaManager::init(BLEServer* server) {
    BLEService* svc = server->createService(OTA_SERVICE_UUID);

    pCtrl = svc->createCharacteristic(
        OTA_CTRL_UUID,
        BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_NOTIFY | BleManager::secureWriteFlags()
    );
    BleManager::addNotifyDescriptor(pCtrl);
    pCtrl->setCallbacks(new OtaCtrlCallbacks());
    BleManager::protectCharacteristic(pCtrl);

    pData = svc->createCharacteristic(
        OTA_DATA_UUID,
        BLECharacteristic::PROPERTY_WRITE | BleManager::secureWriteFlags()
    );
    pData->setCallbacks(new OtaDataCallbacks());
    BleManager::protectCharacteristic(pData);

    svc->start();

    const esp_partition_t* running = esp_ota_get_running_partition();
    Serial.printf("[OTA] BLE OTA готово. Прошивка v%s, раздел: %s\n",
                  FW_VERSION, running ? running->label : "?");
}

void OtaManager::reply(const char* msg) {
    if (!pCtrl) return;
    pCtrl->setValue((uint8_t*)msg, strlen(msg));
    pCtrl->notify();
}

void OtaManager::fail(const char* msg) {
    if (active) {
        Update.abort();
        active = false;
    }
    Serial.printf("[OTA ERROR] %s\n", msg);
    reply(msg);
}

void OtaManager::onCtrlWrite(const uint8_t* data, size_t len) {
    char cmd[96];
    if (len >= sizeof(cmd)) len = sizeof(cmd) - 1;
    memcpy(cmd, data, len);
    cmd[len] = '\0';
    // Обрезаем перевод строки и пробелы в конце
    while (len > 0 && (cmd[len - 1] == '\n' || cmd[len - 1] == '\r' || cmd[len - 1] == ' ')) {
        cmd[--len] = '\0';
    }

    if (strncmp(cmd, "begin ", 6) == 0) {
        handleBegin(cmd + 6);
    } else if (strcmp(cmd, "end") == 0) {
        handleEnd();
    } else if (strcmp(cmd, "abort") == 0) {
        if (active) {
            Update.abort();
            active = false;
        }
        Serial.println("[OTA] Обновление отменено клиентом.");
        reply("OK ABORT");
    } else if (strcmp(cmd, "ver") == 0) {
        const esp_partition_t* running = esp_ota_get_running_partition();
        char buf[64];
        snprintf(buf, sizeof(buf), "VER %s %s", FW_VERSION, running ? running->label : "?");
        reply(buf);
    } else {
        reply("ERR UNKNOWN");
    }
}

void OtaManager::handleBegin(const char* args) {
    if (active) {
        Update.abort();
        active = false;
    }

    unsigned long size = 0;
    unsigned long crc = 0;
    char pin[24] = {0};
    if (sscanf(args, "%lu %lx %23s", &size, &crc, pin) != 3) {
        reply("ERR FORMAT: begin <size> <crc32hex> <pin>");
        return;
    }
    if (strcmp(pin, OTA_PIN) != 0) {
        Serial.println("[OTA] Отказ: неверный PIN.");
        reply("ERR PIN");
        return;
    }

    const esp_partition_t* next = esp_ota_get_next_update_partition(NULL);
    if (!next) {
        reply("ERR NO OTA PARTITION");
        return;
    }
    if (size == 0 || size > next->size) {
        char buf[64];
        snprintf(buf, sizeof(buf), "ERR SIZE (max %u)", (unsigned)next->size);
        reply(buf);
        return;
    }
    if (!Update.begin(size, U_FLASH)) {
        char buf[64];
        snprintf(buf, sizeof(buf), "ERR BEGIN %s", Update.errorString());
        reply(buf);
        return;
    }

    expectedSize = size;
    expectedCrc = crc;
    receivedBytes = 0;
    runningCrc = 0;
    lastProgressReport = 0;
    lastActivityMs = millis();
    active = true;

    Serial.printf("[OTA] Старт обновления: %lu байт -> %s, CRC32 %08lx\n", size, next->label, crc);
    reply("OK BEGIN");
}

void OtaManager::onDataWrite(const uint8_t* data, size_t len) {
    if (!active || len == 0) return;

    if (receivedBytes + len > expectedSize) {
        fail("ERR OVERFLOW");
        return;
    }
    if (Update.write((uint8_t*)data, len) != len) {
        char buf[64];
        snprintf(buf, sizeof(buf), "ERR WRITE %s", Update.errorString());
        fail(buf);
        return;
    }

    runningCrc = esp_rom_crc32_le(runningCrc, data, len);
    receivedBytes += len;
    lastActivityMs = millis();

    if (receivedBytes - lastProgressReport >= 16384 || receivedBytes == expectedSize) {
        lastProgressReport = receivedBytes;
        char buf[24];
        snprintf(buf, sizeof(buf), "PROG %u", (unsigned)receivedBytes);
        reply(buf);
    }
}

void OtaManager::handleEnd() {
    if (!active) {
        reply("ERR NOT ACTIVE");
        return;
    }
    if (receivedBytes != expectedSize) {
        char buf[48];
        snprintf(buf, sizeof(buf), "ERR SIZE %u/%u", (unsigned)receivedBytes, (unsigned)expectedSize);
        fail(buf);
        return;
    }
    if (runningCrc != expectedCrc) {
        char buf[48];
        snprintf(buf, sizeof(buf), "ERR CRC %08x", (unsigned)runningCrc);
        fail(buf);
        return;
    }
    // Update.end() проверяет заголовок образа и SHA-256, затем делает раздел загрузочным
    if (!Update.end(false)) {
        char buf[64];
        snprintf(buf, sizeof(buf), "ERR END %s", Update.errorString());
        active = false;
        Serial.printf("[OTA ERROR] %s\n", buf);
        reply(buf);
        return;
    }

    active = false;
    Serial.println("[OTA] Прошивка принята и проверена. Перезагрузка...");
    reply("OK DONE");
    restartAtMs = millis() + 1500;
    restartPending = true;
}

void OtaManager::onDisconnect() {
    if (active) {
        Update.abort();
        active = false;
        Serial.println("[OTA] Связь разорвана, обновление отменено.");
    }
}

void OtaManager::update() {
    unsigned long now = millis();

    if (active && (now - lastActivityMs > OTA_IDLE_TIMEOUT_MS)) {
        fail("ERR TIMEOUT");
    }

    if (restartPending && (long)(now - restartAtMs) >= 0) {
        ESP.restart();
    }

    if (!appConfirmed && now >= OTA_CONFIRM_UPTIME_MS) {
        appConfirmed = true;
        esp_ota_img_states_t state;
        const esp_partition_t* running = esp_ota_get_running_partition();
        if (running && esp_ota_get_state_partition(running, &state) == ESP_OK &&
            state == ESP_OTA_IMG_PENDING_VERIFY) {
            esp_ota_mark_app_valid_cancel_rollback();
            Serial.println("[OTA] Новая прошивка подтверждена как рабочая (откат отменен).");
        }
    }
}
