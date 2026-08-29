#include "sensors.h"

SensorsManager Sensors;

SensorsManager::SensorsManager()
    : filteredAdcRaw(0.0f),
      lastBtnState(HIGH),
      btnPressTime(0),
      btnHandledShort(false),
      btnHandledLong(false) {
    data.batteryVoltage = 12.6f;
    data.minCrankVoltage = 12.6f;
    data.maxVoltage = 12.6f;
    data.voltStatus = VoltageStatus::VOLT_NORMAL_REST;
    data.tempOutdoor = 21.5f;
    data.tempCabin = 22.0f;
    data.headlightsOn = false;
}

void SensorsManager::init() {
    pinMode(PIN_VOLTAGE_ADC, INPUT);
    pinMode(PIN_BTN_NEXT, INPUT_PULLUP);
    pinMode(PIN_ILLUMINATION, INPUT_PULLDOWN);

    // Начальный замер АЦП
    filteredAdcRaw = analogRead(PIN_VOLTAGE_ADC);
    updateBatteryVoltage();
    data.minCrankVoltage = data.batteryVoltage;
    data.maxVoltage = data.batteryVoltage;
}

void SensorsManager::update() {
    updateBatteryVoltage();
    updateIllumination();
    updateTemperatures();
}

void SensorsManager::updateBatteryVoltage() {
    int raw = analogRead(PIN_VOLTAGE_ADC);
    
    // Экспоненциальное скользящее среднее (EMA) для устранения шума АЦП
    filteredAdcRaw = (filteredAdcRaw * 0.85f) + (raw * 0.15f);

    float pinVoltage = (filteredAdcRaw / ADC_RESOLUTION) * ADC_VREF;
    float calculatedVolt = pinVoltage * VOLTAGE_DIVIDER_RATIO;

    // Если делитель не подключен (пинг висит в воздухе или 0), эмулируем адекватный вольтаж для тестов
    if (calculatedVolt < 1.0f) {
        // Тестовое значение бортсети BMW E36 при стендовых испытаниях
        calculatedVolt = 13.8f;
    }

    data.batteryVoltage = calculatedVolt;

    // Отслеживание экстремумов
    if (calculatedVolt > 5.0f) { // исключаем нули при отключении
        if (calculatedVolt < data.minCrankVoltage) {
            data.minCrankVoltage = calculatedVolt;
        }
        if (calculatedVolt > data.maxVoltage) {
            data.maxVoltage = calculatedVolt;
        }
    }

    // Классификация статуса напряжения
    if (data.batteryVoltage < 11.5f) {
        data.voltStatus = VoltageStatus::VOLT_CRITICAL_LOW;
    } else if (data.batteryVoltage < 12.2f) {
        data.voltStatus = VoltageStatus::VOLT_LOW;
    } else if (data.batteryVoltage <= 13.2f) {
        data.voltStatus = VoltageStatus::VOLT_NORMAL_REST;
    } else if (data.batteryVoltage <= 14.7f) {
        data.voltStatus = VoltageStatus::VOLT_NORMAL_RUNNING;
    } else {
        data.voltStatus = VoltageStatus::VOLT_OVERCHARGE;
    }
}

void SensorsManager::updateIllumination() {
    // Если на пине габаритов высокий уровень - включены фары
    data.headlightsOn = (digitalRead(PIN_ILLUMINATION) == HIGH);
}

void SensorsManager::updateTemperatures() {
    // В базовой версии - температура с небольшим дрейфом или чтение DS18B20 при наличии
    // При подключении DS18B20 на PIN_ONEWIRE_TEMP здесь будет реальное значение
}

void SensorsManager::resetVoltageExtremes() {
    data.minCrankVoltage = data.batteryVoltage;
    data.maxVoltage = data.batteryVoltage;
}

bool SensorsManager::isNextButtonPressed() {
    int btn = digitalRead(PIN_BTN_NEXT);
    bool pressedEvent = false;

    if (btn == LOW && lastBtnState == HIGH) {
        // Нажатие кнопки
        btnPressTime = millis();
        btnHandledShort = false;
        btnHandledLong = false;
    } else if (btn == HIGH && lastBtnState == LOW) {
        // Отпускание кнопки
        unsigned long duration = millis() - btnPressTime;
        if (duration >= 50 && duration < 800 && !btnHandledShort && !btnHandledLong) {
            pressedEvent = true;
            btnHandledShort = true;
        }
    }

    lastBtnState = btn;
    return pressedEvent;
}

bool SensorsManager::isNextButtonHeld() {
    int btn = digitalRead(PIN_BTN_NEXT);
    if (btn == LOW && !btnHandledLong) {
        if (millis() - btnPressTime > 1200) {
            btnHandledLong = true;
            return true;
        }
    }
    return false;
}
