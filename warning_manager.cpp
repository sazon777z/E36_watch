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

void WarningManager::setEnabled(bool en, bool save) {
    if (settings.enabled != en) {
        settings.enabled = en;
        dirtySettings = true;
    }
    if (save) commit();
}

bool WarningManager::setBoostMax(float bar, bool save) {
    if (!(bar > 0.0f && bar <= 3.5f)) return false;
    if (settings.boost_max_bar != bar) {
        settings.boost_max_bar = bar;
        dirtySettings = true;
    }
    if (save) commit();
    return true;
}

bool WarningManager::setCltMax(float degC, bool save) {
    if (!(degC >= 40.0f && degC <= 140.0f)) return false;
    if (settings.clt_max_c != degC) {
        settings.clt_max_c = degC;
        dirtySettings = true;
    }
    if (save) commit();
    return true;
}

bool WarningManager::setAfrLimits(float richMin, float leanMax, bool save) {
    if (!(richMin >= 8.0f && leanMax <= 22.0f && richMin < leanMax)) return false;
    if (settings.afr_rich_min != richMin || settings.afr_lean_max != leanMax) {
        settings.afr_rich_min = richMin;
        settings.afr_lean_max = leanMax;
        dirtySettings = true;
    }
    if (save) commit();
    return true;
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

    // 2. Температура двигателя (COOLANT) - только по реальным данным ЭБУ
    state.cltAlarm = sens.ms2Online && (sens.ms2.clt_c >= settings.clt_max_c);

    // 3. Смесь (AFR)
    // Предупреждение о смеси активно только когда мотор заведен (RPM >= 600)
    // и датчик ШЛЗ выдает достоверные показания (AFR >= 9.0 и AFR <= 22.0).
    // Защита от ложных срабатываний: при торможении двигателем и отсечке топлива (Overrun Fuel Cut)
    // форсунки закрыты, и датчик фиксирует обеднение смеси (20-22 AFR) - это штатная работа ЭБУ.
    if (sens.ms2Online && sens.ms2.rpm >= 600 && sens.ms2.afr >= 9.0f && sens.ms2.afr <= 22.0f) {
        bool isRich = (sens.ms2.afr <= settings.afr_rich_min);
        
        // Бедная смесь опасна только под нагрузкой (педаль нажата или давление во впуске):
        bool isEngineUnderLoad = (sens.ms2.pw1_us >= 1100) && 
                                 (sens.ms2.tps_pct > 5.0f || sens.ms2.map_kpa > 70.0f || sens.ms2.boost_bar > -0.10f);
        
        bool isLean = (sens.ms2.afr >= settings.afr_lean_max) && isEngineUnderLoad;
        state.afrAlarm = isRich || isLean;
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
