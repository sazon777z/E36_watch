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
}

void SensorsManager::init() {
    if (PIN_VOLTAGE_ADC >= 0) {
        pinMode(PIN_VOLTAGE_ADC, INPUT);
        filteredAdcRaw = analogRead(PIN_VOLTAGE_ADC);
    }
    pinMode(PIN_BTN_NEXT, INPUT_PULLUP);
    pinMode(PIN_BTN_BOOT, INPUT_PULLUP);

    updateBatteryVoltage();
    data.minCrankVoltage = data.batteryVoltage;
    data.maxVoltage = data.batteryVoltage;
}

void SensorsManager::update() {
    // Получаем свежие данные CAN-шины
    // Копируем всегда: при потере связи CanDriver сам обнуляет динамические параметры
    data.ms2Online = CanBus.isConnected();
    data.ms2 = CanBus.getTelemetry();

    updateBatteryVoltage();
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

        // Меньше 1 В - делитель не подключен, считаем данные отсутствующими
        data.batteryVoltage = (calculatedVolt < 1.0f) ? 0.0f : calculatedVolt;
    } else {
        // Нет источника напряжения (MS2 offline, АЦП не подключен): 0 = "нет данных"
        data.batteryVoltage = 0.0f;
    }

    // Отслеживание экстремумов
    if (data.batteryVoltage > 5.0f) {
        // Первое реальное значение после старта без данных инициализирует экстремумы
        if (data.minCrankVoltage < 1.0f || data.maxVoltage < 1.0f) {
            data.minCrankVoltage = data.batteryVoltage;
            data.maxVoltage = data.batteryVoltage;
        }
        if (data.batteryVoltage < data.minCrankVoltage) {
            data.minCrankVoltage = data.batteryVoltage;
        }
        if (data.batteryVoltage > data.maxVoltage) {
            data.maxVoltage = data.batteryVoltage;
        }
    }

    // Классификация статуса напряжения
    if (data.batteryVoltage < 1.0f) {
        data.voltStatus = VoltageStatus::VOLT_NORMAL_REST; // Нет данных - без тревоги
    } else if (data.batteryVoltage < 11.5f) {
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

        // Если ожидался второй клик и пауза между нажатиями <= 350 мс - фиксируем ДВОЙНОЙ КЛИК
        if (pendingClick && (now - lastReleaseTime <= 350)) {
            pendingClick = false;
            currentAction = ButtonAction::DOUBLE_CLICK;
            Serial.println("[BTN] Зафиксирован ДВОЙНОЙ КЛИК [DOUBLE_CLICK]");
        }
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

        if (!btnHeldSent && currentAction != ButtonAction::DOUBLE_CLICK && duration >= 30 && duration < 700) {
            pendingClick = true;
            pendingClickTime = now;
            lastReleaseTime = now;
        }
    }
    // Если кнопка отпущена и второй клик не последовал
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
