#include "time_keeper.h"
#include <sys/time.h>
#include <Preferences.h>

TimeKeeper Time;

// RTC-память переживает глубокий сон и программный сброс (но не отключение питания).
// Системное время ESP32 в это время продолжает идти от RTC таймера.
RTC_DATA_ATTR static uint32_t rtcTimeMagic = 0;
RTC_DATA_ATTR static bool rtcTimeSynced = false;
static const uint32_t RTC_TIME_MAGIC = 0xE36C10C4;

const char* TimeKeeper::daysRu[7] = { "ВС", "ПН", "ВТ", "СР", "ЧТ", "ПТ", "СБ" };
const char* TimeKeeper::daysEn[7] = { "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT" };

TimeKeeper::TimeKeeper() : timeSynced(false), timezoneOffset(DEFAULT_TIMEZONE_OFFSET) {
    memset(&currentTime, 0, sizeof(TimeData));
    currentTime.dayOfWeekStrRu = "ПН";
    currentTime.dayOfWeekStrEn = "MON";
}

void TimeKeeper::init() {
    loadTimezoneFromNvs();
    if (rtcTimeMagic == RTC_TIME_MAGIC) {
        // Время сохранилось после сна - не сбрасываем
        timeSynced = rtcTimeSynced;
        update();
        Serial.printf("[TIME] Время сохранено после сна: %s %s\n", currentTime.timeStr, currentTime.dateStr);
        return;
    }
    // Начальное время по умолчанию (если нет синхронизации)
    setManualTime(2026, 8, 30, 12, 0, 0, false);
    update();
}

void TimeKeeper::loadTimezoneFromNvs() {
    Preferences prefs;
    if (prefs.begin("obc_time", true)) {
        timezoneOffset = prefs.getChar("tz", DEFAULT_TIMEZONE_OFFSET);
        prefs.end();
        Serial.printf("[TIME] Загружен часовой пояс из NVS: UTC%+d\n", timezoneOffset);
    }
}

void TimeKeeper::saveTimezoneToNvs() {
    Preferences prefs;
    if (prefs.begin("obc_time", false)) {
        prefs.putChar("tz", timezoneOffset);
        prefs.end();
        Serial.printf("[TIME] Часовой пояс сохранен в NVS: UTC%+d\n", timezoneOffset);
    }
}

void TimeKeeper::setTimezoneOffset(int8_t newOffset) {
    if (newOffset < -12) newOffset = -12;
    if (newOffset > 14) newOffset = 14;

    if (newOffset != timezoneOffset) {
        int8_t diff = newOffset - timezoneOffset;
        timezoneOffset = newOffset;
        saveTimezoneToNvs();

        // Сдвигаем текущие часы на разницу поясов
        time_t now;
        time(&now);
        time_t shifted = now + (time_t)diff * 3600;
        struct timeval tv = { .tv_sec = shifted, .tv_usec = 0 };
        settimeofday(&tv, NULL);
        update();
    }
}

void TimeKeeper::update() {
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    rtcTimeMagic = RTC_TIME_MAGIC;
    rtcTimeSynced = timeSynced;

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

void TimeKeeper::setEpoch(time_t epoch) {
    // epoch передается как UTC секунды, сдвигаем на текущий часовой пояс
    time_t local_sec = epoch + (time_t)timezoneOffset * 3600;
    struct timeval tv = { .tv_sec = local_sec, .tv_usec = 0 };
    settimeofday(&tv, NULL);
    timeSynced = true;
    update();
}

void TimeKeeper::setTimeOnly(int hour, int minute, int second) {
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    timeinfo.tm_hour = hour;
    timeinfo.tm_min = minute;
    timeinfo.tm_sec = second;
    timeinfo.tm_isdst = -1;

    time_t t_new = mktime(&timeinfo);
    struct timeval tv = { .tv_sec = t_new, .tv_usec = 0 };
    settimeofday(&tv, NULL);
    timeSynced = true;
    update();
}

void TimeKeeper::setDateOnly(int day, int month, int year) {
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    timeinfo.tm_mday = day;
    timeinfo.tm_mon = month - 1;
    timeinfo.tm_year = year - 1900;
    timeinfo.tm_isdst = -1;

    time_t t_new = mktime(&timeinfo);
    struct timeval tv = { .tv_sec = t_new, .tv_usec = 0 };
    settimeofday(&tv, NULL);
    timeSynced = true;
    update();
}

void TimeKeeper::setManualTime(int year, int month, int day, int hour, int minute, int second, bool isSynced) {
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
    timeSynced = isSynced;
    update();
}

unsigned long TimeKeeper::getUptimeSeconds() const {
    return millis() / 1000UL;
}
