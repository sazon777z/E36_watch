#ifndef CAN_DRIVER_H
#define CAN_DRIVER_H

#include <Arduino.h>
#include <driver/twai.h>
#include "config.h"

// Структура живой телеметрии из ЭБУ MegaSquirt 2
struct Ms2Telemetry {
    // Основные параметры двигателя
    uint16_t rpm;           // Обороты двигателя (об/мин)
    float map_kpa;          // Абсолютное давление во впуске (кПа)
    float boost_bar;        // Избыточное давление наддува / разряжение (Бар)
    float clt_c;            // Температура охлаждающей жидкости / ДВС (°C)
    float mat_c;            // Температура воздуха на впуске IAT / MAT (°C)
    float tps_pct;          // Положение дроссельной заслонки (%)
    float batt_volt;        // Напряжение бортсети с АЦП MS2 (Вольт)
    float afr;              // Реальная смесь (ШЛЗ лямбда / AFR)
    float afr_target;       // Целевая смесь (Target AFR)
    float advance_deg;      // Угол опережения зажигания (УОЗ, град.)
    uint16_t pw1_us;        // Время впрыска форсунок (мкс)
    uint16_t speed_kmh;     // Скорость автомобиля (км/ч, если подключен датчик к MS2)
    uint8_t engine_flags;   // Флаги состояния (cranking, warmup, etc.)

    // Диагностика связи CAN
    bool isOnline;          // Флаг активного приема пакетов
    uint32_t packetsTotal;  // Всего принято пакетов
    uint32_t lastPacketMs;  // Время последнего пакета (millis)
};

class CanDriver {
public:
    CanDriver();

    bool init();
    void update();

    const Ms2Telemetry& getTelemetry() const { return telemetry; }
    bool isConnected() const { return telemetry.isOnline; }

private:
    Ms2Telemetry telemetry;
    bool isDriverInstalled;

    void parsePacket(const twai_message_t& msg);
    void parseMs2RealtimeAdv(uint32_t canId, const uint8_t* data, uint8_t len);
    void parseMs2Dash(uint32_t canId, const uint8_t* data, uint8_t len);
};

extern CanDriver CanBus;

#endif // CAN_DRIVER_H
