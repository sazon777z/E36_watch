#include "warning_manager.h"

WarningManager Warnings;
static Preferences warnPrefs;

WarningManager::WarningManager()
    : lastBlinkToggleMs(0),
      dirtySettings(false) {
    settings.enabled = true;
    settings.boost_max_bar = 1.20f;
    settings.clt_max_c = 100.0f;
    settings.afr_lean_max = 15.5f;
    settings.afr_rich_min = 10.5f;

    state.boostAlarm = false;
    state.cltAlarm = false;
    state.afrAlarm = false;
    state.blinkPhase = false;
}

void WarningManager::init() {
    loadFromNvs();
    Serial.printf("[WARN] Инициализация варнингов: EN=%d, BoostMax=%.2fb, CltMax=%.1fC, AFR=[%.1f..%.1f]\n",
                  settings.enabled ? 1 : 0, settings.boost_max_bar, settings.clt_max_c,
                  settings.afr_rich_min, settings.afr_lean_max);
}

void WarningManager::loadFromNvs() {
    warnPrefs.begin("e36_warn", true);
    settings.enabled = warnPrefs.getBool("en", true);
    settings.boost_max_bar = warnPrefs.getFloat("boost", 1.20f);
    settings.clt_max_c = warnPrefs.getFloat("clt", 100.0f);
    settings.afr_lean_max = warnPrefs.getFloat("afr_max", 15.5f);
    settings.afr_rich_min = warnPrefs.getFloat("afr_min", 10.5f);
    warnPrefs.end();
}

void WarningManager::saveToNvs() {
    warnPrefs.begin("e36_warn", false);
    warnPrefs.putBool("en", settings.enabled);
    warnPrefs.putFloat("boost", settings.boost_max_bar);
    warnPrefs.putFloat("clt", settings.clt_max_c);
    warnPrefs.putFloat("afr_max", settings.afr_lean_max);
    warnPrefs.putFloat("afr_min", settings.afr_rich_min);
    warnPrefs.end();
    dirtySettings = false;
    Serial.println("[WARN NVS] Пороги предупреждений сохранены во Flash.");
}

void WarningManager::setEnabled(bool en) {
    settings.enabled = en;
    saveToNvs();
}

void WarningManager::setBoostMax(float bar) {
    if (bar > 0.0f && bar <= 3.5f) {
        settings.boost_max_bar = bar;
        saveToNvs();
    }
}

void WarningManager::setCltMax(float degC) {
    if (degC >= 40.0f && degC <= 140.0f) {
        settings.clt_max_c = degC;
        saveToNvs();
    }
}

void WarningManager::setAfrLimits(float richMin, float leanMax) {
    if (richMin >= 8.0f && leanMax <= 22.0f && richMin < leanMax) {
        settings.afr_rich_min = richMin;
        settings.afr_lean_max = leanMax;
        saveToNvs();
    }
}

void WarningManager::update(const SensorData& sens) {
    unsigned long now = millis();

    // Переключение фазы стробоскопа каждые 250 мс (~4 Гц)
    if (now - lastBlinkToggleMs >= 250) {
        state.blinkPhase = !state.blinkPhase;
        lastBlinkToggleMs = now;
    }

    if (!settings.enabled) {
        state.boostAlarm = false;
        state.cltAlarm = false;
        state.afrAlarm = false;
        return;
    }

    // 1. Наддув (BOOST)
    if (sens.ms2Online) {
        state.boostAlarm = (sens.ms2.boost_bar >= settings.boost_max_bar);
    } else {
        state.boostAlarm = false;
    }

    // 2. Температура двигателя (COOLANT)
    float curClt = sens.ms2Online ? sens.ms2.clt_c : sens.tempOutdoor;
    state.cltAlarm = (curClt >= settings.clt_max_c);

    // 3. Смесь (AFR)
    // Предупреждение о смеси активно только когда мотор заведен (RPM >= 500)
    // и датчик ШЛЗ выдает достоверные показания (AFR >= 9.0)
    if (sens.ms2Online && sens.ms2.rpm >= 500 && sens.ms2.afr >= 9.0f) {
        state.afrAlarm = (sens.ms2.afr >= settings.afr_lean_max || sens.ms2.afr <= settings.afr_rich_min);
    } else {
        state.afrAlarm = false;
    }
}

String WarningManager::getSettingsJson() const {
    char buf[160];
    snprintf(buf, sizeof(buf),
             "{\"warn_en\":%d,\"boost_max\":%.2f,\"clt_max\":%.1f,\"afr_min\":%.1f,\"afr_max\":%.1f}",
             settings.enabled ? 1 : 0,
             settings.boost_max_bar,
             settings.clt_max_c,
             settings.afr_rich_min,
             settings.afr_lean_max);
    return String(buf);
}
