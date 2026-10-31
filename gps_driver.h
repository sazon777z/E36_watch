#ifndef GPS_DRIVER_H
#define GPS_DRIVER_H

#include <Arduino.h>
#include "config.h"

struct GpsData {
    float speed_kmh;        // Текущая скорость (км/ч)
    uint8_t satellites;     // Количество видимых спутников в решении
    bool hasFix;            // Флаг активного 2D/3D позиционирования
    double latitude;        // Широта
    double longitude;       // Долгота
    float altitude_m;       // Высота над уровнем моря (м)
    float hdop;             // Геометрический фактор точности (меньше - лучше, 99 = нет данных)
    float rawSpeed_kmh;     // Скорость от модуля без фильтра стоянки (для диагностики)
    bool moving;            // Фильтр признал движение реальным

    // Атомное время со спутников (UTC)
    int year;
    int month;
    int day;
    int hour;
    int minute;
    int second;
    bool timeValid;
    bool timeSyncedToRtc;

    uint32_t lastFixMs;
    uint32_t lastSentenceMs;
    uint32_t sentencesParsed;
};

class GpsDriver {
public:
    GpsDriver();

    bool init();
    void update();

    const GpsData& getData() const { return data; }
    bool isConnected() const;
    bool hasValidFix() const { return data.hasFix; }

private:
    GpsData data;
    char buffer[128];
    uint8_t bufIdx;
    unsigned long lastRtcSyncMillis;
    uint8_t moveConfirmCount;

    float filterSpeed(float kmh);

    void parseSentence(char* sentence);
    void parseRMC(char* f[], int count);
    void parseGGA(char* f[], int count);
    bool verifyChecksum(const char* s);
    void syncRtcTime();
};

extern GpsDriver Gps;

#endif // GPS_DRIVER_H
