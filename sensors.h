#ifndef SENSORS_H
#define SENSORS_H

#include <Arduino.h>
#include "config.h"
#include "can_driver.h"

enum class VoltageStatus {
    VOLT_CRITICAL_LOW,   // < 11.5V (Сильный разряд АКБ)
    VOLT_LOW,            // 11.5V - 12.2V (Низкий заряд)
    VOLT_NORMAL_REST,    // 12.2V - 12.8V (Заглушен, норма)
    VOLT_NORMAL_RUNNING, // 13.5V - 14.6V (Запущен, идет заряд генератора)
    VOLT_OVERCHARGE      // > 14.8V (Перезаряд / неисправность реле-регулятора)
};

struct SensorData {
    float batteryVoltage;
    float minCrankVoltage;
    float maxVoltage;
    VoltageStatus voltStatus;
    float tempOutdoor;
    float tempCabin;
    bool headlightsOn;
    bool ms2Online;
    Ms2Telemetry ms2;
};

enum class ButtonAction {
    NONE = 0,
    CLICK,
    DOUBLE_CLICK,
    HOLD
};

class SensorsManager {
public:
    SensorsManager();

    void init();
    void update();

    const SensorData& getData() const { return data; }
    void resetVoltageExtremes();

    // Проверка кнопки переключения экранов и режимов
    ButtonAction getButtonAction();
    bool isNextButtonPressed();
    bool isNextButtonDoubleClicked();
    bool isNextButtonHeld();

private:
    SensorData data;
    float filteredAdcRaw;

    // Кнопка
    int lastBtnState;
    unsigned long btnPressTime;
    unsigned long lastReleaseTime;
    unsigned long pendingClickTime;
    bool btnPressed;
    bool btnHeldSent;
    bool pendingClick;
    ButtonAction currentAction;

    void updateButtonState();
    void updateBatteryVoltage();
    void updateIllumination();
    void updateTemperatures();
};

extern SensorsManager Sensors;

#endif // SENSORS_H
