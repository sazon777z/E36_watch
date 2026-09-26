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

class TripComputer {
public:
    TripComputer();

    void init();
    void update();

    void resetTrip();
    void setTotalOdometer(float km);

    const TripData& getData() const { return data; }

private:
    TripData data;
    unsigned long lastUpdateMillis;
    unsigned long lastSaveMillis;
    float lastSavedOdoKm;

    void loadFromNvs();
    void saveToNvs(bool force = false);
    float getFuelRateLitersPerHour();
};

extern TripComputer Trip;

#endif // TRIP_COMPUTER_H
