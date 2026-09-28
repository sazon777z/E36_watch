#ifndef TRIP_COMPUTER_H
#define TRIP_COMPUTER_H

#include <Arduino.h>
#include "config.h"

struct TripData {
    float current_speed_kmh;        // Скорость по GPS (км/ч)
    float trip_distance_km;         // Суточный пробег (км)
    float total_odometer_km;        // Общий пробег автомобиля (км)
    
    float instant_consumption;      // Мгновенный расход (л/100км или л/ч)
    bool isLitersPerHour;           // Флаг: true = л/ч (стоянка), false = л/100км (движение)
    
    float avg_consumption_l_100km;  // Средний расход за поездку (л/100км)
    float trip_fuel_liters;         // Израсходовано топлива за поездку (литры)
    float avg_speed_kmh;            // Средняя скорость за поездку (км/ч)
    uint32_t trip_time_sec;         // Время движения в поездке (сек)
};

struct TripPeaks {
    float peak_boost_bar;     // Максимальный наддув за поездку (бар)
    float max_clt_c;          // Максимальная температура ОЖ (°C)
    float min_afr;            // Минимальная смесь AFR (наиболее богатая)
    float max_afr;            // Максимальная смесь AFR (наиболее бедная)
    float max_speed_kmh;      // Максимальная скорость по GPS (км/ч)
    uint16_t max_rpm;         // Максимальные обороты двигателя (RPM)
    float max_iat_c;          // Максимальная температура впуска IAT (°C)
    float peak_instant_fuel;  // Пиковый мгновенный расход (л/100км)
};

class TripComputer {
public:
    TripComputer();

    void init();
    void update();

    void resetTrip();
    void resetPeaks();
    void setTotalOdometer(float km);

    const TripData& getData() const { return data; }
    const TripPeaks& getPeaks() const { return peaks; }

private:
    TripData data;
    TripPeaks peaks;
    unsigned long lastUpdateMillis;
    unsigned long lastSaveMillis;
    float lastSavedOdoKm;

    void loadFromNvs();
    void saveToNvs(bool force = false);
    float getFuelRateLitersPerHour();
};

extern TripComputer Trip;

#endif // TRIP_COMPUTER_H
