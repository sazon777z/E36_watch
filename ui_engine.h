#ifndef UI_ENGINE_H
#define UI_ENGINE_H

#include <Arduino.h>
#include "display_driver.h"
#include "time_keeper.h"
#include "sensors.h"
#include "bmw_theme.h"
#include "bmw_assets.h"

enum class ScreenId {
    BOOT_SPLASH = 0,
    CLASSIC_CLOCK,
    OBC_TRIP_FUEL,
    OBC_TELEMETRY,
    M_PERFORMANCE,
    SETTINGS_INFO,
    COUNT
};

class UiEngine {
public:
    UiEngine();

    void init();
    void update();

    // Переключение экранов
    void nextScreen();
    void setScreen(ScreenId screen);
    ScreenId getCurrentScreen() const { return currentScreen; }

    // Заставка загрузки
    void showBootSplash(const char* subtitle = "ON-BOARD COMPUTER");

    // Секундомер / таймер M-Performance
    void toggleStopwatch();
    void resetStopwatch();

private:
    ScreenId currentScreen;
    ScreenId lastScreen;
    bool needsFullRedraw;

    // Кеш предыдущих значений для устранения мерцания
    int lastHour;
    int lastMinute;
    int lastSecond;
    int lastDay;
    float lastVoltage;
    float lastTemp;
    bool lastBleState;
    bool colonState;
    unsigned long lastColonBlinkMillis;

    // Секундомер
    bool stopwatchRunning;
    unsigned long stopwatchStartMillis;
    unsigned long stopwatchElapsedMillis;

    // Отрисовка конкретных экранов
    void drawClassicClockScreen(bool fullRedraw);
    void drawObcTripFuelScreen(bool fullRedraw);
    void drawObcTelemetryScreen(bool fullRedraw);
    void drawMPerformanceScreen(bool fullRedraw);
    void drawSettingsInfoScreen(bool fullRedraw);

    // Вспомогательные элементы
    void drawGpsCornerIndicator(bool forceRedraw = false);
    void drawHeader(const char* title, bool showStatusIcons = true);
    void drawFooter(const char* leftText, const char* rightText);

    // Кеш спутников GPS
    int lastGpsSats;
    bool lastGpsFix;

    // Кеш значений для полного устранения мерцания (dirty check)
    char lastClockBoostStr[16];
    char lastClockCltStr[16];
    char lastClockAfrStr[16];

    char lastSpdStr[8];
    char lastInstStr[16];
    char lastTripDistStr[16];
    char lastAvgFuelStr[16];
    char lastOdoStr[16];
    char lastTripFuelStr[16];
    bool lastFixDisplay;

    char lastTelemVoltStr[16];
    char lastTelemCltStr[16];
    char lastTelemAfrStr[16];
    char lastTelemTpsStr[16];
    char lastTelemBoostStr[32];
    int lastTelemBoostW;

    int lastRpmVal;
    int lastShiftBarW;
    char lastMBoostStr[16];
    char lastMAdvStr[16];
};

extern UiEngine UI;

#endif // UI_ENGINE_H
