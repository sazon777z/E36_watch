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

    // Установка времени через BLE или вручную
    void setEpoch(time_t epoch);
    void setManualTime(int year, int month, int day, int hour, int minute, int second);
    void setTimeOnly(int hour, int minute, int second);
    void setDateOnly(int day, int month, int year);

    // Текущие данные времени
    const TimeData& getTime() const { return currentTime; }
    bool isTimeSynced() const { return timeSynced; }
    unsigned long getUptimeSeconds() const;

private:
    TimeData currentTime;
    bool timeSynced;

    static const char* daysRu[7];
    static const char* daysEn[7];
};

extern TimeKeeper Time;

#endif // TIME_KEEPER_H
