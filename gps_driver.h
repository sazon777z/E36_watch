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

    void parseSentence(char* sentence);
    void parseRMC(char* s);
    void parseGGA(char* s);
    bool verifyChecksum(const char* s);
    void syncRtcTime();
};

extern GpsDriver Gps;

#endif // GPS_DRIVER_H
