#ifndef SENSORS_H
#define SENSORS_H

#include <Arduino.h>
#include "config.h"

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
};

class SensorsManager {
public:
    SensorsManager();

    void init();
    void update();

    const SensorData& getData() const { return data; }
    void resetVoltageExtremes();

    // Проверка кнопки переключения экранов
    bool isNextButtonPressed();
    bool isNextButtonHeld();

private:
    SensorData data;
    float filteredAdcRaw;

    // Кнопка
    int lastBtnState;
    unsigned long btnPressTime;
    bool btnHandledShort;
    bool btnHandledLong;

    void updateBatteryVoltage();
    void updateIllumination();
    void updateTemperatures();
};

extern SensorsManager Sensors;

#endif // SENSORS_H
