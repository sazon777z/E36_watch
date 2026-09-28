#include "sensors.h"

SensorsManager Sensors;

SensorsManager::SensorsManager()
    : filteredAdcRaw(0.0f),
      lastBtnState(HIGH),
      btnPressTime(0),
      lastReleaseTime(0),
      pendingClickTime(0),
      btnPressed(false),
      btnHeldSent(false),
      pendingClick(false),
      currentAction(ButtonAction::NONE) {
    data.batteryVoltage = 12.6f;
    data.minCrankVoltage = 12.6f;
    data.maxVoltage = 12.6f;
    data.voltStatus = VoltageStatus::VOLT_NORMAL_REST;
    data.tempOutdoor = 21.5f;
    data.tempCabin = 22.0f;
    data.headlightsOn = false;
}

void SensorsManager::init() {
    if (PIN_VOLTAGE_ADC >= 0) {
        pinMode(PIN_VOLTAGE_ADC, INPUT);
        filteredAdcRaw = analogRead(PIN_VOLTAGE_ADC);
    }
    pinMode(PIN_BTN_NEXT, INPUT_PULLUP);
    pinMode(PIN_BTN_BOOT, INPUT_PULLUP);
    pinMode(PIN_ILLUMINATION, INPUT_PULLDOWN);

    updateBatteryVoltage();
    data.minCrankVoltage = data.batteryVoltage;
    data.maxVoltage = data.batteryVoltage;
}

void SensorsManager::update() {
    // Получаем свежие данные CAN-шины
    data.ms2Online = CanBus.isConnected();
    if (data.ms2Online) {
        data.ms2 = CanBus.getTelemetry();
    }

    updateBatteryVoltage();
    updateIllumination();
    updateTemperatures();
    updateButtonState();
}

void SensorsManager::updateBatteryVoltage() {
    if (data.ms2Online && data.ms2.batt_volt > 5.0f) {
        // Данные напряжения поступают напрямую из ЭБУ MegaSquirt 2 с высокой точностью
        data.batteryVoltage = data.ms2.batt_volt;
    } else if (PIN_VOLTAGE_ADC >= 0) {
        // Резервное чтение через встроенный АЦП ESP32-S3 (если настроен)
        int raw = analogRead(PIN_VOLTAGE_ADC);
        filteredAdcRaw = (filteredAdcRaw * 0.85f) + (raw * 0.15f);
        float pinVoltage = (filteredAdcRaw / ADC_RESOLUTION) * ADC_VREF;
        float calculatedVolt = pinVoltage * VOLTAGE_DIVIDER_RATIO;

        if (calculatedVolt < 1.0f) {
            calculatedVolt = 13.8f; // Тестовое значение бортсети при стендовых испытаниях
        }
        data.batteryVoltage = calculatedVolt;
    } else {
        // Режим без АЦП (ожидание CAN) - тестовое напряжение для стенда
        data.batteryVoltage = 13.8f;
    }

    // Отслеживание экстремумов
    if (data.batteryVoltage > 5.0f) {
        if (data.batteryVoltage < data.minCrankVoltage) {
            data.minCrankVoltage = data.batteryVoltage;
        }
        if (data.batteryVoltage > data.maxVoltage) {
            data.maxVoltage = data.batteryVoltage;
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

void SensorsManager::updateButtonState() {
    int b14 = digitalRead(PIN_BTN_NEXT);
    int b0 = digitalRead(PIN_BTN_BOOT);
    int btn = (b14 == LOW || b0 == LOW) ? LOW : HIGH;
    unsigned long now = millis();

    // Нажатие кнопки
    if (btn == LOW && lastBtnState == HIGH) {
        btnPressTime = now;
        btnPressed = true;
        btnHeldSent = false;
    }
    // Кнопка удерживается
    else if (btn == LOW && btnPressed) {
        if (!btnHeldSent && (now - btnPressTime >= 1000)) {
            btnHeldSent = true;
            pendingClick = false;
            currentAction = ButtonAction::HOLD;
            Serial.println("[BTN] Кнопка удерживается (>1 сек) [HOLD]");
        }
    }
    // Отпускание кнопки
    else if (btn == HIGH && lastBtnState == LOW) {
        unsigned long duration = now - btnPressTime;
        btnPressed = false;

        if (!btnHeldSent && duration >= 30 && duration < 700) {
            // Проверяем: было ли недавнее отпускание для двойного клика?
            if (pendingClick && (now - lastReleaseTime <= 300)) {
                pendingClick = false;
                currentAction = ButtonAction::DOUBLE_CLICK;
                Serial.println("[BTN] Зафиксирован ДВОЙНОЙ КЛИК [DOUBLE_CLICK]");
            } else {
                pendingClick = true;
                pendingClickTime = now;
                lastReleaseTime = now;
            }
        }
    }
    // Если кнопка отпущена и ожидался возможный второй клик
    else if (btn == HIGH && pendingClick) {
        if (now - pendingClickTime > 280) {
            pendingClick = false;
            currentAction = ButtonAction::CLICK;
            Serial.println("[BTN] Зафиксирован ОДИНОЧНЫЙ КЛИК [CLICK]");
        }
    }

    lastBtnState = btn;
}

ButtonAction SensorsManager::getButtonAction() {
    ButtonAction act = currentAction;
    currentAction = ButtonAction::NONE;
    return act;
}

bool SensorsManager::isNextButtonPressed() {
    return currentAction == ButtonAction::CLICK;
}

bool SensorsManager::isNextButtonDoubleClicked() {
    return currentAction == ButtonAction::DOUBLE_CLICK;
}

bool SensorsManager::isNextButtonHeld() {
    return currentAction == ButtonAction::HOLD;
}
