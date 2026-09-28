#ifndef WARNING_MANAGER_H
#define WARNING_MANAGER_H

#include <Arduino.h>
#include <Preferences.h>
#include "sensors.h"

// Структура настраиваемых порогов предупреждений
struct WarningSettings {
    bool enabled;            // Глобальное включение/выключение варнингов
    float boost_max_bar;     // Порог максимального наддува (передув, Бар)
    float clt_max_c;         // Порог перегрева охлаждающей жидкости (°C)
    float afr_lean_max;      // Порог слишком бедной смеси (AFR, например 15.5)
    float afr_rich_min;      // Порог слишком богатой смеси (AFR, например 10.5)
};

// Структура текущего состояния аварийных сигналов
struct WarningState {
    bool boostAlarm;         // Активен варнинг по наддуву
    bool cltAlarm;           // Активен варнинг по температуре
    bool afrAlarm;           // Активен варнинг по смеси
    bool blinkPhase;         // Фаза мигания (true = подсветка, false = пауза)
};

class WarningManager {
public:
    WarningManager();

    void init();
    void update(const SensorData& sens);

    // Доступ к состоянию и настройкам
    const WarningSettings& getSettings() const { return settings; }
    const WarningState& getState() const { return state; }

    // Установка параметров
    void setEnabled(bool en);
    void setBoostMax(float bar);
    void setCltMax(float degC);
    void setAfrLimits(float richMin, float leanMax);

    // Сохранение и загрузка из NVS Flash
    void loadFromNvs();
    void saveToNvs();

    // Генерация строки настроек в формате JSON для мобильного приложения
    String getSettingsJson() const;

private:
    WarningSettings settings;
    WarningState state;

    unsigned long lastBlinkToggleMs;
    bool dirtySettings;
};

extern WarningManager Warnings;

#endif // WARNING_MANAGER_H
