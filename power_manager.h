#ifndef POWER_MANAGER_H
#define POWER_MANAGER_H

#include <Arduino.h>
#include <esp_sleep.h>
#include "config.h"

class PowerManager {
public:
    PowerManager();

    void init();
    void update();

    // Сброс таймера бездействия (кнопка, команды)
    void notifyActivity() { lastActivityMs = millis(); }

    // Немедленный уход в глубокий сон (не возвращается)
    void enterDeepSleep();

    bool wokeFromSleep() const { return wakeCause != ESP_SLEEP_WAKEUP_UNDEFINED; }
    const char* getWakeReasonStr() const;

private:
    unsigned long lastActivityMs;
    esp_sleep_wakeup_cause_t wakeCause;
    uint64_t wakePins;
};

extern PowerManager Power;

#endif // POWER_MANAGER_H
