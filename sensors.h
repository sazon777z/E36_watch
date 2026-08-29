#ifndef SENSORS_H
#define SENSORS_H

#include <Arduino.h>
#include "config.h"

enum class VoltageStatus {
    CRITICAL_LOW,   // < 11.5V (Сильный разряд АКБ)
    LOW,            // 11.5V - 12.2V (Низкий заряд)
    NORMAL_REST,    // 12.2V - 12.8V (Заглушен, норма)
    NORMAL_RUNNING, // 13.5V - 14.6V (Запущен, идет заряд генератора)
    OVERCHARGE      // > 14.8V (Перезаряд / неисправность реле-регулятора)
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
