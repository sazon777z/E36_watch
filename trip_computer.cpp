#include "trip_computer.h"
#include "gps_driver.h"
#include "can_driver.h"
#include <Preferences.h>

TripComputer Trip;
static Preferences prefs;

TripComputer::TripComputer()
    : lastUpdateMillis(0),
      lastSaveMillis(0),
      lastSavedOdoKm(0.0f) {
    memset(&data, 0, sizeof(TripData));
    data.total_odometer_km = 245000.0f; // Значение по умолчанию
    data.isLitersPerHour = true;
    resetPeaks();
}

void TripComputer::init() {
    loadFromNvs();
    lastUpdateMillis = millis();
    lastSaveMillis = millis();
    Serial.printf("[TRIP] Инициализация: Общий одометр = %.1f км, Суточный = %.2f км\n",
                  data.total_odometer_km, data.trip_distance_km);
}

void TripComputer::loadFromNvs() {
    prefs.begin("e36_trip", false);
    data.total_odometer_km = prefs.getFloat("total_km", 245000.0f);
    data.trip_distance_km = prefs.getFloat("trip_km", 0.0f);
    data.trip_fuel_liters = prefs.getFloat("trip_fuel", 0.0f);
    data.trip_time_sec = prefs.getUInt("trip_time", 0);
    prefs.end();

    lastSavedOdoKm = data.total_odometer_km;
}

void TripComputer::saveToNvs(bool force) {
    // Сохраняем во Flash только при приросте от 1 км или по таймеру раз в 5 минут,
    // чтобы сберечь ресурс циклов перезаписи памяти EEPROM/NVS
    if (force || (data.total_odometer_km - lastSavedOdoKm >= 1.0f) || (millis() - lastSaveMillis > 300000UL)) {
        prefs.begin("e36_trip", false);
        prefs.putFloat("total_km", data.total_odometer_km);
        prefs.putFloat("trip_km", data.trip_distance_km);
        prefs.putFloat("trip_fuel", data.trip_fuel_liters);
        prefs.putUInt("trip_time", data.trip_time_sec);
        prefs.end();

        lastSavedOdoKm = data.total_odometer_km;
        lastSaveMillis = millis();
        Serial.printf("[TRIP NVS] Сохранен одометр: %.1f км\n", data.total_odometer_km);
    }
}

void TripComputer::resetPeaks() {
    peaks.peak_boost_bar = 0.0f;
    peaks.max_clt_c = 0.0f;
    peaks.min_afr = 99.0f;
    peaks.max_afr = 0.0f;
    peaks.max_speed_kmh = 0.0f;
    peaks.max_rpm = 0;
    peaks.max_iat_c = 0.0f;
    peaks.peak_instant_fuel = 0.0f;
}

void TripComputer::resetTrip() {
    data.trip_distance_km = 0.0f;
    data.trip_fuel_liters = 0.0f;
    data.avg_consumption_l_100km = 0.0f;
    data.trip_time_sec = 0;
    data.avg_speed_kmh = 0.0f;
    resetPeaks();
    saveToNvs(true);
    Serial.println("[TRIP] Суточный одометр и пиковые значения сброшены.");
}

void TripComputer::setTotalOdometer(float km) {
    if (km >= 0.0f) {
        data.total_odometer_km = km;
        saveToNvs(true);
        Serial.printf("[TRIP] Установлен общий пробег: %.1f км\n", km);
    }
}

float TripComputer::getFuelRateLitersPerHour() {
    const Ms2Telemetry& ms2 = CanBus.getTelemetry();

    if (!ms2.isOnline || ms2.rpm < 300 || ms2.pw1_us <= INJECTOR_DEAD_TIME_US) {
        return 0.0f;
    }

    // Эффективное время впрыска (за вычетом мертвого времени форсунки)
    float effectivePwMs = (float)(ms2.pw1_us - INJECTOR_DEAD_TIME_US) / 1000.0f;

    // Скважность открытия форсунок при 1 впрыске на оборот коленвала
    float dutyCycle = ((float)ms2.rpm * effectivePwMs) / 120000.0f;
    dutyCycle = constrain(dutyCycle, 0.0f, 0.95f);

    // Суммарный объем впрыска всех цилиндров (сс/мин)
    float totalFlowCcMin = dutyCycle * INJECTOR_FLOW_CC_MIN * (float)ENGINE_CYLINDERS;

    // Перевод в литры в час (L/h)
    float litersPerHour = (totalFlowCcMin * 60.0f / 1000.0f) * FUEL_CALIBRATION_FACTOR;

    return litersPerHour;
}

void TripComputer::update() {
    unsigned long now = millis();
    if (lastUpdateMillis == 0) {
        lastUpdateMillis = now;
        return;
    }

    float dt = (float)(now - lastUpdateMillis) / 1000.0f;
    lastUpdateMillis = now;

    if (dt <= 0.0f || dt > 2.0f) return;

    // 1. Получаем скорость от GPS
    data.current_speed_kmh = Gps.getData().speed_kmh;

    // 2. Расчет пройденной дистанции (при скорости > 1.8 км/ч отсекаем шум GPS стоянки)
    if (data.current_speed_kmh > 1.8f) {
        float deltaKm = data.current_speed_kmh * (dt / 3600.0f);
        data.trip_distance_km += deltaKm;
        data.total_odometer_km += deltaKm;
        data.trip_time_sec += (uint32_t)dt;
    }

    // 3. Расчет расхода топлива
    float fuelRateLh = getFuelRateLitersPerHour();

    if (fuelRateLh > 0.05f) {
        // Накапливаем общий расход в поездке
        data.trip_fuel_liters += fuelRateLh * (dt / 3600.0f);

        // Мгновенный расход (аутентичная логика BMW OBC)
        if (data.current_speed_kmh > 3.0f) {
            // В движении: литры на 100 км
            data.instant_consumption = (fuelRateLh / data.current_speed_kmh) * 100.0f;
            data.instant_consumption = constrain(data.instant_consumption, 0.0f, 99.9f);
            data.isLitersPerHour = false;
        } else {
            // На стоянке/холостых: литры в час
            data.instant_consumption = fuelRateLh;
            data.isLitersPerHour = true;
        }
    } else {
        data.instant_consumption = 0.0f;
        data.isLitersPerHour = (data.current_speed_kmh <= 3.0f);
    }

    // 4. Средний расход за поездку (L / 100 km)
    if (data.trip_distance_km >= 0.05f) {
        data.avg_consumption_l_100km = (data.trip_fuel_liters / data.trip_distance_km) * 100.0f;
    }

    // 5. Средняя скорость
    if (data.trip_time_sec > 5) {
        data.avg_speed_kmh = (data.trip_distance_km / (float)data.trip_time_sec) * 3600.0f;
    }

    // 6. Обновление пиковых параметров за поездку
    if (data.current_speed_kmh > peaks.max_speed_kmh) {
        peaks.max_speed_kmh = data.current_speed_kmh;
    }
    if (!data.isLitersPerHour && data.instant_consumption > peaks.peak_instant_fuel) {
        peaks.peak_instant_fuel = data.instant_consumption;
    }

    const Ms2Telemetry& ms2 = CanBus.getTelemetry();
    if (ms2.isOnline) {
        if (ms2.boost_bar > peaks.peak_boost_bar) {
            peaks.peak_boost_bar = ms2.boost_bar;
        }
        if (ms2.clt_c > peaks.max_clt_c) {
            peaks.max_clt_c = ms2.clt_c;
        }
        if (ms2.rpm > peaks.max_rpm) {
            peaks.max_rpm = ms2.rpm;
        }
        if (ms2.mat_c > peaks.max_iat_c) {
            peaks.max_iat_c = ms2.mat_c;
        }
        if (ms2.rpm > 600 && ms2.afr > 7.0f && ms2.afr < 25.0f) {
            if (ms2.afr < peaks.min_afr) {
                peaks.min_afr = ms2.afr;
            }
            if (ms2.afr > peaks.max_afr) {
                peaks.max_afr = ms2.afr;
            }
        }
    }

    // 7. Периодическое сохранение одометра в энергонезависимую память
    saveToNvs(false);
}
