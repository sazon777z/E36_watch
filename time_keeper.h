#ifndef TIME_KEEPER_H
#define TIME_KEEPER_H

#include <Arduino.h>
#include <time.h>
#include "config.h"

struct TimeData {
    int hour;
    int minute;
    int second;
    int day;
    int month;
    int year;
    int dayOfWeek; // 0 = ВС, 1 = ПН, ..., 6 = СБ
    char timeStr[9];    // "HH:MM:SS"
    char dateStr[11];   // "DD.MM.YYYY"
    const char* dayOfWeekStrRu; // "ПН", "ВТ", ...
    const char* dayOfWeekStrEn; // "MON", "TUE", ...
};

class TimeKeeper {
public:
    TimeKeeper();

    void init();
    void update();

    // Синхронизация через Wi-Fi NTP
    bool syncNtp(int gmtOffsetHours = DEFAULT_TIMEZONE_OFFSET, int daylightOffsetSec = 0);

    // Ручная установка времени
    void setManualTime(int year, int month, int day, int hour, int minute, int second);

    // Текущие данные времени
    const TimeData& getTime() const { return currentTime; }
    bool isTimeSynced() const { return ntpSynced; }
    unsigned long getUptimeSeconds() const;

private:
    TimeData currentTime;
    bool ntpSynced;
    unsigned long lastNtpSyncMillis;

    static const char* daysRu[7];
    static const char* daysEn[7];
};

extern TimeKeeper Time;

#endif // TIME_KEEPER_H
