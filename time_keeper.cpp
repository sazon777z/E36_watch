#include "time_keeper.h"
#include <WiFi.h>
#include <sys/time.h>

TimeKeeper Time;

const char* TimeKeeper::daysRu[7] = { "ВС", "ПН", "ВТ", "СР", "ЧТ", "ПТ", "СБ" };
const char* TimeKeeper::daysEn[7] = { "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT" };

TimeKeeper::TimeKeeper() : ntpSynced(false), lastNtpSyncMillis(0) {
    memset(&currentTime, 0, sizeof(TimeData));
    currentTime.dayOfWeekStrRu = "ПН";
    currentTime.dayOfWeekStrEn = "MON";
}

void TimeKeeper::init() {
    // Начальное время по умолчанию (если нет синхронизации)
    setManualTime(2026, 8, 30, 12, 0, 0);
    update();
}

void TimeKeeper::update() {
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    currentTime.hour = timeinfo.tm_hour;
    currentTime.minute = timeinfo.tm_min;
    currentTime.second = timeinfo.tm_sec;
    currentTime.day = timeinfo.tm_mday;
    currentTime.month = timeinfo.tm_mon + 1;
    currentTime.year = timeinfo.tm_year + 1900;
    currentTime.dayOfWeek = timeinfo.tm_wday; // 0 = Sunday

    snprintf(currentTime.timeStr, sizeof(currentTime.timeStr), "%02d:%02d:%02d",
             currentTime.hour, currentTime.minute, currentTime.second);
    snprintf(currentTime.dateStr, sizeof(currentTime.dateStr), "%02d.%02d.%04d",
             currentTime.day, currentTime.month, currentTime.year);

    if (currentTime.dayOfWeek >= 0 && currentTime.dayOfWeek <= 6) {
        currentTime.dayOfWeekStrRu = daysRu[currentTime.dayOfWeek];
        currentTime.dayOfWeekStrEn = daysEn[currentTime.dayOfWeek];
    }
}

bool TimeKeeper::syncNtp(int gmtOffsetHours, int daylightOffsetSec) {
    if (WiFi.status() != WL_CONNECTED) {
        return false;
    }

    long gmtOffsetSec = gmtOffsetHours * 3600;
    configTime(gmtOffsetSec, daylightOffsetSec, NTP_SERVER_1, NTP_SERVER_2);

    struct tm timeinfo;
    // Ожидание синхронизации до 2 секунд
    if (getLocalTime(&timeinfo, 2000)) {
        ntpSynced = true;
        lastNtpSyncMillis = millis();
        update();
        return true;
    }

    return false;
}

void TimeKeeper::setManualTime(int year, int month, int day, int hour, int minute, int second) {
    struct tm t;
    t.tm_year = year - 1900;
    t.tm_mon = month - 1;
    t.tm_mday = day;
    t.tm_hour = hour;
    t.tm_min = minute;
    t.tm_sec = second;
    t.tm_isdst = -1;

    time_t t_of_day = mktime(&t);
    struct timeval tv = { .tv_sec = t_of_day, .tv_usec = 0 };
    settimeofday(&tv, NULL);
    update();
}

unsigned long TimeKeeper::getUptimeSeconds() const {
    return millis() / 1000UL;
}
