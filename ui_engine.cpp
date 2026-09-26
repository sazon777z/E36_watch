#include "ui_engine.h"
#include "ble_manager.h"
#include "gps_driver.h"
#include "trip_computer.h"
#include <Fonts/FreeSansBoldOblique9pt7b.h>
#include <Fonts/FreeSansBoldOblique12pt7b.h>
#include <Fonts/FreeSansBoldOblique18pt7b.h>
#include "bmw_clock_font.h"

UiEngine UI;

UiEngine::UiEngine()
    : currentScreen(ScreenId::CLASSIC_CLOCK),
      lastScreen(ScreenId::BOOT_SPLASH),
      needsFullRedraw(true),
      lastHour(-1),
      lastMinute(-1),
      lastSecond(-1),
      lastDay(-1),
      lastVoltage(-1.0f),
      lastTemp(-99.0f),
      lastBleState(false),
      colonState(true),
      lastColonState(true),
      lastColonBlinkMillis(0),
      stopwatchRunning(false),
      stopwatchStartMillis(0),
      stopwatchElapsedMillis(0),
      lastGpsSats(-1),
      lastGpsFix(false),
      lastFixDisplay(false),
      lastTelemBoostW(-1),
      lastRpmVal(-1),
      lastShiftBarW(-1) {
    lastClockBoostStr[0] = '\0';
    lastClockCltStr[0] = '\0';
    lastClockAfrStr[0] = '\0';
    lastSpdStr[0] = '\0';
    lastInstStr[0] = '\0';
    lastTripDistStr[0] = '\0';
    lastAvgFuelStr[0] = '\0';
    lastOdoStr[0] = '\0';
    lastTripFuelStr[0] = '\0';
    lastTelemVoltStr[0] = '\0';
    lastTelemCltStr[0] = '\0';
    lastTelemAfrStr[0] = '\0';
    lastTelemTpsStr[0] = '\0';
    lastTelemBoostStr[0] = '\0';
    lastMBoostStr[0] = '\0';
    lastMAdvStr[0] = '\0';
}

void UiEngine::init() {
    needsFullRedraw = true;
}

void UiEngine::nextScreen() {
    int next = ((int)currentScreen + 1);
    if (next >= (int)ScreenId::COUNT) {
        next = (int)ScreenId::CLASSIC_CLOCK; // Пропускаем заставку при циклическом переключении
    }
    setScreen((ScreenId)next);
}

void UiEngine::setScreen(ScreenId screen) {
    if (currentScreen != screen) {
        currentScreen = screen;
        needsFullRedraw = true;
    }
}

void UiEngine::showBootSplash(const char* subtitle) {
    Adafruit_ST7789& tft = Display.getTft();
    Display.setBrightness(0);
    tft.fillScreen(COLOR_BLACK);

    // Лаконичная заставка Megasquirt 2 без лишних графических логотипов
    tft.setFont(&FreeSansBoldOblique18pt7b);
    tft.setTextColor(COLOR_WHITE);
    tft.setCursor(48, 105);
    tft.print("Megasquirt 2");

    // Тонкая разделительная линия
    tft.drawFastHLine(48, 120, 224, COLOR_DARK_GRAY);

    tft.setFont(&FreeSansBoldOblique9pt7b);
    tft.setTextColor(COLOR_SILVER);
    tft.setCursor(68, 145);
    tft.print("CAN-BUS TELEMETRY");

    tft.setFont(); // Сброс шрифта на стандартный

    // Плавный розжиг подсветки
    Display.fadeIn(DEFAULT_BRIGHTNESS, 4);
    delay(800);
    Display.fadeOut(3);
    
    tft.fillScreen(COLOR_BLACK);
    setScreen(ScreenId::CLASSIC_CLOCK);
    Display.fadeIn(DEFAULT_BRIGHTNESS, 3);
}

void UiEngine::drawGpsCornerIndicator(bool forceRedraw) {
    const GpsData& gps = Gps.getData();
    if (!forceRedraw && gps.satellites == lastGpsSats && gps.hasFix == lastGpsFix) {
        return; // Без изменений - исключаем мерцание
    }

    lastGpsSats = gps.satellites;
    lastGpsFix = gps.hasFix;

    Adafruit_ST7789& tft = Display.getTft();

    // Очистка зоны правого верхнего угла (X: 256..318, Y: 6..32)
    tft.fillRect(256, 6, 62, 26, COLOR_BLACK);

    uint16_t col = gps.hasFix ? COLOR_WHITE : COLOR_MID_GRAY;

    // Укрупненная иконка спутника (16x16 px)
    BmwAssets::drawLargeSatellite(tft, 258, 10, col, gps.hasFix);

    // Крупное количество спутников (FreeSansBoldOblique12pt7b)
    tft.setFont(&FreeSansBoldOblique12pt7b);
    tft.setTextColor(col);
    tft.setCursor(282, 25);
    if (gps.hasFix || gps.satellites > 0) {
        tft.printf("%d", gps.satellites);
    } else {
        tft.print("--");
    }
    tft.setFont(); // Сброс шрифта
}

void UiEngine::drawHeader(const char* title, bool showStatusIcons) {
    // Верхняя мелкая строка статуса полностью отключена для максимального минимализма
}

void UiEngine::drawFooter(const char* leftText, const char* rightText) {
    // Нижняя мелкая строка подвала полностью отключена для максимального минимализма
}

// -----------------------------------------------------------------------------
// ЭКРАН 1: ЛАКОНИЧНЫЕ ЧАСЫ (MINIMALIST SLANTED BOLD CLOCK)
// -----------------------------------------------------------------------------
void UiEngine::drawClassicClockScreen(bool fullRedraw) {
    Adafruit_ST7789& tft = Display.getTft();
    const TimeData& td = Time.getTime();
    const SensorData& sens = Sensors.getData();

    if (fullRedraw) {
        tft.fillScreen(COLOR_BLACK);

        // Тонкая разделительная линия перед блоками телеметрии
        tft.drawFastHLine(15, 126, 290, COLOR_DARK_GRAY);

        // Подписи нижних блоков телеметрии (FreeSansBoldOblique9pt7b с идеальными промежутками)
        tft.setFont(&FreeSansBoldOblique9pt7b);
        tft.setTextColor(COLOR_SILVER);
        tft.setCursor(16, 152);
        tft.print("BOOST");
        tft.setCursor(118, 152);
        tft.print("COOLANT");
        tft.setCursor(228, 152);
        tft.print("AFR");

        lastHour = -1;
        lastMinute = -1;
        lastSecond = -1;
        lastColonState = false;
        lastClockBoostStr[0] = '\0';
        lastClockCltStr[0] = '\0';
        lastClockAfrStr[0] = '\0';
        lastGpsSats = -1;
    }

    // Мигание наклонного двоеточия раз в секунду
    if (millis() - lastColonBlinkMillis >= 500) {
        colonState = !colonState;
        lastColonBlinkMillis = millis();
    }

    // Отрисовка ЧАСОВ нативным высокодетализированным шрифтом BmwClockFont (1:1, без ступеней и обрубленности)
    if (fullRedraw || td.hour != lastHour) {
        lastHour = td.hour;
        char hBuf[4];
        snprintf(hBuf, sizeof(hBuf), "%02d", td.hour);
        tft.fillRect(68, 44, 80, 56, COLOR_BLACK);
        tft.setFont(&BmwClockFont);
        tft.setTextSize(1);
        tft.setTextColor(COLOR_WHITE);
        tft.setCursor(70, 96);
        tft.print(hBuf);
    }

    // Двоеточие между часами и минутами (мигание)
    if (fullRedraw || colonState != lastColonState) {
        lastColonState = colonState;
        tft.fillRect(148, 44, 23, 56, COLOR_BLACK);
        if (colonState) {
            tft.setFont(&BmwClockFont);
            tft.setTextSize(1);
            tft.setTextColor(COLOR_WHITE);
            tft.setCursor(148, 96);
            tft.print(":");
        }
    }

    // МИНУТЫ
    if (fullRedraw || td.minute != lastMinute) {
        lastMinute = td.minute;
        char mBuf[4];
        snprintf(mBuf, sizeof(mBuf), "%02d", td.minute);
        tft.fillRect(170, 44, 80, 56, COLOR_BLACK);
        tft.setFont(&BmwClockFont);
        tft.setTextSize(1);
        tft.setTextColor(COLOR_WHITE);
        tft.setCursor(171, 96);
        tft.print(mBuf);
    }

    // Обновление нижних значений телеметрии (ТОЛЬКО ПРИ ИЗМЕНЕНИИ СТРОКИ ДЛЯ ИСКЛЮЧЕНИЯ МЕРЦАНИЯ)
    tft.setFont(&FreeSansBoldOblique18pt7b);
    tft.setTextColor(COLOR_WHITE);

    // 1. Наддув (BOOST)
    char curBoostStr[16];
    if (sens.ms2Online) {
        snprintf(curBoostStr, sizeof(curBoostStr), "%+.2fb", sens.ms2.boost_bar);
    } else {
        snprintf(curBoostStr, sizeof(curBoostStr), "0.00b");
    }
    if (fullRedraw || strcmp(curBoostStr, lastClockBoostStr) != 0) {
        strncpy(lastClockBoostStr, curBoostStr, sizeof(lastClockBoostStr));
        tft.fillRect(14, 172, 96, 42, COLOR_BLACK);
        tft.setCursor(14, 206);
        tft.print(curBoostStr);
    }

    // 2. Температура ОЖ (COOLANT)
    char curCltStr[16];
    snprintf(curCltStr, sizeof(curCltStr), "%+.0fC", sens.tempOutdoor);
    if (fullRedraw || strcmp(curCltStr, lastClockCltStr) != 0) {
        strncpy(lastClockCltStr, curCltStr, sizeof(lastClockCltStr));
        tft.fillRect(118, 172, 96, 42, COLOR_BLACK);
        tft.setCursor(118, 206);
        tft.print(curCltStr);
    }

    // 3. Смесь AFR (вместо LAMBDA, без наложения на COOLANT)
    char curAfrStr[16];
    if (sens.ms2Online) {
        snprintf(curAfrStr, sizeof(curAfrStr), "%.1f", sens.ms2.afr);
    } else {
        snprintf(curAfrStr, sizeof(curAfrStr), "--.-");
    }
    if (fullRedraw || strcmp(curAfrStr, lastClockAfrStr) != 0) {
        strncpy(lastClockAfrStr, curAfrStr, sizeof(lastClockAfrStr));
        tft.fillRect(226, 172, 90, 42, COLOR_BLACK);
        tft.setCursor(228, 206);
        tft.print(curAfrStr);
    }

    tft.setFont(); // Сброс шрифта

    // Крупный индикатор спутников в правом верхнем углу
    drawGpsCornerIndicator(fullRedraw);
}

// -----------------------------------------------------------------------------
// ЭКРАН 2: БОРТОВОЙ КОМПЬЮТЕР, ОДОМЕТР И РАСХОД ТОПЛИВА (OBC TRIP & FUEL)
// -----------------------------------------------------------------------------
void UiEngine::drawObcTripFuelScreen(bool fullRedraw) {
    Adafruit_ST7789& tft = Display.getTft();
    const TripData& trip = Trip.getData();
    const GpsData& gps = Gps.getData();

    if (fullRedraw) {
        tft.fillScreen(COLOR_BLACK);

        // Горизонтальные разделители
        tft.drawFastHLine(10, 86, 300, COLOR_DARK_GRAY);
        tft.drawFastHLine(10, 158, 300, COLOR_DARK_GRAY);
        tft.drawFastVLine(158, 92, 138, COLOR_DARK_GRAY);

        // Статические подписи (жирный наклонный)
        tft.setFont(&FreeSansBoldOblique9pt7b);
        tft.setTextColor(COLOR_SILVER);

        tft.setCursor(16, 106);
        tft.print("TRIP DISTANCE:");

        tft.setCursor(168, 106);
        tft.print("AVG CONSUMPTION:");

        tft.setCursor(16, 178);
        tft.print("TOTAL ODOMETER:");

        tft.setCursor(168, 178);
        tft.print("TRIP FUEL:");

        lastSpdStr[0] = '\0';
        lastInstStr[0] = '\0';
        lastTripDistStr[0] = '\0';
        lastAvgFuelStr[0] = '\0';
        lastOdoStr[0] = '\0';
        lastTripFuelStr[0] = '\0';
        lastFixDisplay = !gps.hasFix;
        lastGpsSats = -1;
    }

    // 1. СКОРОСТЬ GPS (Крупные жирные наклонные цифры) - только при изменении!
    char spdBuf[8];
    int spd = (int)trip.current_speed_kmh;
    if (spd > 999) spd = 999;
    snprintf(spdBuf, sizeof(spdBuf), "%3d", spd);

    if (fullRedraw || strcmp(spdBuf, lastSpdStr) != 0) {
        strncpy(lastSpdStr, spdBuf, sizeof(lastSpdStr));
        BmwAssets::drawSlantedBoldDigit(tft, 14, 18, spdBuf[0], COLOR_WHITE, COLOR_GHOST_SEG, 4, 12);
        BmwAssets::drawSlantedBoldDigit(tft, 52, 18, spdBuf[1], COLOR_WHITE, COLOR_GHOST_SEG, 4, 12);
        BmwAssets::drawSlantedBoldDigit(tft, 90, 18, spdBuf[2], COLOR_WHITE, COLOR_GHOST_SEG, 4, 12);
    }

    // Подпись KM/H и статус фиксации
    if (fullRedraw || gps.hasFix != lastFixDisplay) {
        lastFixDisplay = gps.hasFix;
        tft.setFont(&FreeSansBoldOblique9pt7b);
        tft.setTextColor(COLOR_SILVER);
        tft.setCursor(132, 40);
        tft.print("KM/H");
        if (!gps.hasFix) {
            tft.setTextColor(COLOR_MID_GRAY);
            tft.setCursor(130, 62);
            tft.print("NO FIX");
        } else {
            tft.fillRect(130, 50, 42, 20, COLOR_BLACK);
        }
    }

    // 2. МГНОВЕННЫЙ РАСХОД - только при изменении!
    char curInstStr[16];
    snprintf(curInstStr, sizeof(curInstStr), "%.1f %s", 
             trip.instant_consumption, 
             trip.isLitersPerHour ? "L/H" : "L");
    if (fullRedraw || strcmp(curInstStr, lastInstStr) != 0) {
        strncpy(lastInstStr, curInstStr, sizeof(lastInstStr));
        tft.fillRect(170, 14, 84, 68, COLOR_BLACK);
        tft.setCursor(172, 32);
        tft.setTextColor(COLOR_SILVER);
        tft.setFont(&FreeSansBoldOblique9pt7b);
        tft.print(trip.isLitersPerHour ? "INST. L/H:" : "INSTANT:");

        tft.setFont(&FreeSansBoldOblique18pt7b);
        tft.setTextColor(COLOR_WHITE);
        tft.setCursor(172, 70);
        tft.printf("%.1f", trip.instant_consumption);

        if (!trip.isLitersPerHour) {
            tft.setFont(&FreeSansBoldOblique9pt7b);
            tft.setTextColor(COLOR_SILVER);
            tft.setCursor(224, 70);
            tft.print("L");
        }
    }

    // 3. КВАДРАНТ 1: Суточный пробег - только при изменении!
    char curTripDistStr[16];
    snprintf(curTripDistStr, sizeof(curTripDistStr), "%.1f km", trip.trip_distance_km);
    if (fullRedraw || strcmp(curTripDistStr, lastTripDistStr) != 0) {
        strncpy(lastTripDistStr, curTripDistStr, sizeof(lastTripDistStr));
        tft.fillRect(16, 114, 138, 38, COLOR_BLACK);
        tft.setFont(&FreeSansBoldOblique18pt7b);
        tft.setTextColor(COLOR_WHITE);
        tft.setCursor(16, 144);
        tft.print(curTripDistStr);
    }

    // 4. КВАДРАНТ 2: Средний расход - только при изменении!
    char curAvgFuelStr[16];
    if (trip.trip_distance_km >= 0.1f) {
        snprintf(curAvgFuelStr, sizeof(curAvgFuelStr), "%.1f L", trip.avg_consumption_l_100km);
    } else {
        snprintf(curAvgFuelStr, sizeof(curAvgFuelStr), "--- L");
    }
    if (fullRedraw || strcmp(curAvgFuelStr, lastAvgFuelStr) != 0) {
        strncpy(lastAvgFuelStr, curAvgFuelStr, sizeof(lastAvgFuelStr));
        tft.fillRect(168, 114, 148, 38, COLOR_BLACK);
        tft.setFont(&FreeSansBoldOblique18pt7b);
        tft.setTextColor(COLOR_WHITE);
        tft.setCursor(168, 144);
        tft.print(curAvgFuelStr);
    }

    // 5. КВАДРАНТ 3: Общий одометр - только при изменении!
    char curOdoStr[16];
    snprintf(curOdoStr, sizeof(curOdoStr), "%.0f km", trip.total_odometer_km);
    if (fullRedraw || strcmp(curOdoStr, lastOdoStr) != 0) {
        strncpy(lastOdoStr, curOdoStr, sizeof(lastOdoStr));
        tft.fillRect(16, 186, 138, 38, COLOR_BLACK);
        tft.setFont(&FreeSansBoldOblique18pt7b);
        tft.setTextColor(COLOR_WHITE);
        tft.setCursor(16, 216);
        tft.print(curOdoStr);
    }

    // 6. КВАДРАНТ 4: Топливо поездки - только при изменении!
    char curTripFuelStr[16];
    snprintf(curTripFuelStr, sizeof(curTripFuelStr), "%.1f L", trip.trip_fuel_liters);
    if (fullRedraw || strcmp(curTripFuelStr, lastTripFuelStr) != 0) {
        strncpy(lastTripFuelStr, curTripFuelStr, sizeof(lastTripFuelStr));
        tft.fillRect(168, 186, 148, 38, COLOR_BLACK);
        tft.setFont(&FreeSansBoldOblique18pt7b);
        tft.setTextColor(COLOR_WHITE);
        tft.setCursor(168, 216);
        tft.print(curTripFuelStr);
    }

    tft.setFont(); // Сброс шрифта

    // Крупный индикатор спутников в правом верхнем углу
    drawGpsCornerIndicator(fullRedraw);
}

// -----------------------------------------------------------------------------
// ЭКРАН 3: ТЕЛЕМЕТРИЯ И БОРТОВОЙ КОМПЬЮТЕР (OBC TELEMETRY & ENGINE)
// -----------------------------------------------------------------------------
void UiEngine::drawObcTelemetryScreen(bool fullRedraw) {
    Adafruit_ST7789& tft = Display.getTft();
    const SensorData& sens = Sensors.getData();

    if (fullRedraw) {
        tft.fillScreen(COLOR_BLACK);

        // Тонкие разделительные линии координатной сетки (1 px)
        tft.drawFastHLine(10, 84, 300, COLOR_DARK_GRAY);
        tft.drawFastHLine(10, 164, 300, COLOR_DARK_GRAY);
        tft.drawFastVLine(158, 10, 150, COLOR_DARK_GRAY);

        // Статические подписи параметров (FreeSansBoldOblique9pt7b)
        tft.setFont(&FreeSansBoldOblique9pt7b);
        tft.setTextColor(COLOR_SILVER);
        tft.setCursor(15, 24);  tft.print("BATTERY");
        tft.setCursor(168, 24); tft.print("COOLANT");
        tft.setCursor(15, 104); tft.print("AIR / FUEL (AFR)");
        tft.setCursor(168, 104);tft.print("THROTTLE (TPS)");

        lastTelemVoltStr[0] = '\0';
        lastTelemCltStr[0] = '\0';
        lastTelemAfrStr[0] = '\0';
        lastTelemTpsStr[0] = '\0';
        lastTelemBoostStr[0] = '\0';
        lastTelemBoostW = -1;
        lastGpsSats = -1;
    }

    // 1. НАПРЯЖЕНИЕ АКБ (Верх-лево) - только при изменении!
    char curVoltStr[16];
    snprintf(curVoltStr, sizeof(curVoltStr), "%.2fV", sens.batteryVoltage);
    if (fullRedraw || strcmp(curVoltStr, lastTelemVoltStr) != 0) {
        strncpy(lastTelemVoltStr, curVoltStr, sizeof(lastTelemVoltStr));
        tft.fillRect(15, 30, 138, 50, COLOR_BLACK);
        tft.setFont(&FreeSansBoldOblique18pt7b);
        tft.setTextColor(COLOR_WHITE);
        tft.setCursor(15, 56);
        tft.print(curVoltStr);

        tft.setFont(&FreeSansBoldOblique9pt7b);
        tft.setTextColor(COLOR_SILVER);
        tft.setCursor(15, 74);
        if (sens.voltStatus == VoltageStatus::VOLT_CRITICAL_LOW) {
            tft.setTextColor(COLOR_STATUS_ERR);
            tft.print("LOW BATTERY!");
        } else if (sens.voltStatus == VoltageStatus::VOLT_NORMAL_RUNNING) {
            tft.print("ALT: CHARGING");
        } else {
            tft.printf("CRK: %.1fV", sens.minCrankVoltage);
        }
    }

    // 2. ТЕМПЕРАТУРА ОЖ ДВС (Верх-право) - только при изменении!
    char curCltStr[16];
    snprintf(curCltStr, sizeof(curCltStr), "%+.0f C", sens.tempOutdoor);
    if (fullRedraw || strcmp(curCltStr, lastTelemCltStr) != 0) {
        strncpy(lastTelemCltStr, curCltStr, sizeof(lastTelemCltStr));
        tft.fillRect(168, 30, 86, 50, COLOR_BLACK);
        tft.setFont(&FreeSansBoldOblique18pt7b);
        tft.setTextColor(COLOR_WHITE);
        tft.setCursor(168, 56);
        tft.print(curCltStr);

        tft.setFont(&FreeSansBoldOblique9pt7b);
        tft.setTextColor(COLOR_SILVER);
        tft.setCursor(168, 74);
        tft.printf("INTAKE: %+.0f C", sens.tempCabin);
    }

    // 3. СМЕСЬ AFR / ЛЯМБДА (Низ-лево) - только при изменении!
    char curAfrStr[16];
    if (sens.ms2Online) {
        snprintf(curAfrStr, sizeof(curAfrStr), "%.1f", sens.ms2.afr);
    } else {
        snprintf(curAfrStr, sizeof(curAfrStr), "--.-");
    }
    if (fullRedraw || strcmp(curAfrStr, lastTelemAfrStr) != 0) {
        strncpy(lastTelemAfrStr, curAfrStr, sizeof(lastTelemAfrStr));
        tft.fillRect(15, 110, 138, 50, COLOR_BLACK);
        tft.setFont(&FreeSansBoldOblique18pt7b);
        tft.setTextColor(COLOR_WHITE);
        tft.setCursor(15, 136);
        tft.print(curAfrStr);

        tft.setFont(&FreeSansBoldOblique9pt7b);
        tft.setTextColor(COLOR_SILVER);
        tft.setCursor(15, 154);
        if (sens.ms2Online) {
            tft.printf("TARGET: %.1f", sens.ms2.afr_target);
        } else {
            tft.print("NO CAN DATA");
        }
    }

    // 4. ДРОССЕЛЬ TPS (Низ-право) - только при изменении!
    char curTpsStr[16];
    if (sens.ms2Online) {
        snprintf(curTpsStr, sizeof(curTpsStr), "%.0f%%", sens.ms2.tps_pct);
    } else {
        snprintf(curTpsStr, sizeof(curTpsStr), "--%%");
    }
    if (fullRedraw || strcmp(curTpsStr, lastTelemTpsStr) != 0) {
        strncpy(lastTelemTpsStr, curTpsStr, sizeof(lastTelemTpsStr));
        tft.fillRect(168, 110, 140, 50, COLOR_BLACK);
        tft.setFont(&FreeSansBoldOblique18pt7b);
        tft.setTextColor(COLOR_WHITE);
        tft.setCursor(168, 136);
        tft.print(curTpsStr);

        tft.setFont(&FreeSansBoldOblique9pt7b);
        tft.setTextColor(COLOR_SILVER);
        tft.setCursor(168, 154);
        if (sens.ms2Online) {
            tft.printf("MAP: %.0f kPa", sens.ms2.map_kpa);
        } else {
            tft.print("TPS NO CAN");
        }
    }

    // 5. НИЖНЯЯ ШКАЛА НАДДУВА (BOOST / MAP BAR) - только при изменении!
    char curBoostStr[32];
    if (sens.ms2Online) {
        snprintf(curBoostStr, sizeof(curBoostStr), "%+.2f Bar (%d kPa)", sens.ms2.boost_bar, (int)sens.ms2.map_kpa);
    } else {
        snprintf(curBoostStr, sizeof(curBoostStr), "0.00 Bar (100 kPa)");
    }

    float boostVal = sens.ms2Online ? sens.ms2.boost_bar : 0.0f;
    float clampedBoost = constrain(boostVal, -0.8f, 1.5f);
    int boostW = (int)((clampedBoost + 0.8f) / 2.3f * 280.0f);

    if (fullRedraw || strcmp(curBoostStr, lastTelemBoostStr) != 0 || boostW != lastTelemBoostW) {
        strncpy(lastTelemBoostStr, curBoostStr, sizeof(lastTelemBoostStr));
        lastTelemBoostW = boostW;

        tft.fillRect(15, 170, 290, 24, COLOR_BLACK);

        tft.setFont(&FreeSansBoldOblique9pt7b);
        tft.setTextColor(COLOR_SILVER);
        tft.setCursor(18, 188);
        tft.print("BOOST:");

        tft.setFont(&FreeSansBoldOblique12pt7b);
        tft.setTextColor(COLOR_WHITE);
        tft.setCursor(88, 188);
        tft.print(curBoostStr);

        // Полоса наддува
        tft.drawRect(18, 198, 284, 20, COLOR_DARK_GRAY);
        tft.fillRect(20, 200, 280, 16, COLOR_BLACK);
        if (boostW > 0) {
            tft.fillRect(20, 200, boostW, 16, COLOR_WHITE);
        }
    }
    tft.setFont();

    // Крупный индикатор спутников в правом верхнем углу
    drawGpsCornerIndicator(fullRedraw);
}

// -----------------------------------------------------------------------------
// ЭКРАН 3: СПОРТИВНЫЙ ЭКРАН ///M PERFORMANCE & ТАХОМЕТР
// -----------------------------------------------------------------------------
void UiEngine::drawMPerformanceScreen(bool fullRedraw) {
    Adafruit_ST7789& tft = Display.getTft();
    const SensorData& sens = Sensors.getData();

    if (fullRedraw) {
        tft.fillScreen(COLOR_BLACK);

        // Статические метки шкалы оборотов (0 .. 7.5k)
        tft.setTextColor(COLOR_SILVER);
        tft.setTextSize(1);
        tft.setCursor(20, 124);  tft.print("0");
        tft.setCursor(85, 124);  tft.print("2k");
        tft.setCursor(150, 124); tft.print("4k");
        tft.setCursor(215, 124); tft.print("6k");
        tft.setCursor(275, 124); tft.print("7.5k");

        // Тонкая разделительная линия перед нижними параметрами
        tft.drawFastHLine(15, 142, 290, COLOR_DARK_GRAY);

        // Подписи нижних приборов (BOOST и IGNITION ADVANCE)
        tft.setFont(&FreeSansBoldOblique12pt7b);
        tft.setTextColor(COLOR_SILVER);
        tft.setCursor(20, 168);
        tft.print("BOOST");
        tft.setCursor(168, 168);
        tft.print("ADVANCE");

        // Подпись RPM жирным наклонным шрифтом
        tft.setFont(&FreeSansBoldOblique18pt7b);
        tft.setTextColor(COLOR_WHITE);
        tft.setCursor(185, 60);
        tft.print("RPM");

        tft.drawRect(18, 88, 284, 24, COLOR_DARK_GRAY);
        tft.setFont();

        lastRpmVal = -1;
        lastShiftBarW = -1;
        lastMBoostStr[0] = '\0';
        lastMAdvStr[0] = '\0';
        lastGpsSats = -1;
    }

    if (sens.ms2Online) {
        // Отрисовка жирных наклонных цифр оборотов двигателя - только при изменении
        if (fullRedraw || (sens.ms2.rpm / 20) != (lastRpmVal / 20)) {
            lastRpmVal = sens.ms2.rpm;
            char rpmBuf[6];
            snprintf(rpmBuf, sizeof(rpmBuf), "%4d", sens.ms2.rpm);

            BmwAssets::drawSlantedBoldDigit(tft, 18, 18, rpmBuf[0], COLOR_WHITE, COLOR_GHOST_SEG, 4, 12);
            BmwAssets::drawSlantedBoldDigit(tft, 56, 18, rpmBuf[1], COLOR_WHITE, COLOR_GHOST_SEG, 4, 12);
            BmwAssets::drawSlantedBoldDigit(tft, 94, 18, rpmBuf[2], COLOR_WHITE, COLOR_GHOST_SEG, 4, 12);
            BmwAssets::drawSlantedBoldDigit(tft, 132, 18, rpmBuf[3], COLOR_WHITE, COLOR_GHOST_SEG, 4, 12);
        }

        // Прогрессивная полоса тахометра (Shift-Bar, 0 - 7500 RPM)
        int rpmW = map(constrain((int)sens.ms2.rpm, 0, 7500), 0, 7500, 0, 280);
        if (fullRedraw || abs(rpmW - lastShiftBarW) >= 3) {
            lastShiftBarW = rpmW;
            tft.fillRect(20, 90, 280, 20, COLOR_BLACK);
            if (rpmW > 0) {
                uint16_t barCol = (sens.ms2.rpm >= 6500) ? COLOR_BMW_M_RED : COLOR_WHITE;
                tft.fillRect(20, 90, rpmW, 20, barCol);
            }
        }

        // Нижние спортивные показатели (Наддув, УОЗ) шрифтом 18pt
        char curBoostStr[16];
        snprintf(curBoostStr, sizeof(curBoostStr), "%+.2fb", sens.ms2.boost_bar);
        if (fullRedraw || strcmp(curBoostStr, lastMBoostStr) != 0) {
            strncpy(lastMBoostStr, curBoostStr, sizeof(lastMBoostStr));
            tft.fillRect(20, 178, 136, 46, COLOR_BLACK);
            tft.setFont(&FreeSansBoldOblique18pt7b);
            tft.setTextColor(COLOR_WHITE);
            tft.setCursor(20, 210);
            tft.print(curBoostStr);
        }

        char curAdvStr[16];
        snprintf(curAdvStr, sizeof(curAdvStr), "%.1f*", sens.ms2.advance_deg);
        if (fullRedraw || strcmp(curAdvStr, lastMAdvStr) != 0) {
            strncpy(lastMAdvStr, curAdvStr, sizeof(lastMAdvStr));
            tft.fillRect(168, 178, 136, 46, COLOR_BLACK);
            tft.setFont(&FreeSansBoldOblique18pt7b);
            tft.setTextColor(COLOR_WHITE);
            tft.setCursor(168, 210);
            tft.print(curAdvStr);
        }

        tft.setFont(); // Сброс

    } else {
        // MS2 оффлайн: статус ожидания CAN пакетов (замеры отключены)
        if (fullRedraw) {
            tft.fillRect(25, 40, 270, 50, COLOR_BLACK);
            tft.setFont(&FreeSansBoldOblique12pt7b);
            tft.setTextColor(COLOR_MID_GRAY);
            tft.setCursor(35, 78);
            tft.print("WAITING MS2 CAN...");
            tft.setFont();
        }
    }

    // Крупный индикатор спутников в правом верхнем углу
    drawGpsCornerIndicator(fullRedraw);
}

void UiEngine::toggleStopwatch() {
    // Замеры отключены по запросу пользователя
}

void UiEngine::resetStopwatch() {
    // Замеры отключены по запросу пользователя
}

// -----------------------------------------------------------------------------
// ЭКРАН 4: ИНФОРМАЦИЯ И НАСТРОЙКИ WI-FI ПОРТАЛА
// -----------------------------------------------------------------------------
void UiEngine::drawSettingsInfoScreen(bool fullRedraw) {
    Adafruit_ST7789& tft = Display.getTft();
    const SensorData& sens = Sensors.getData();

    if (fullRedraw) {
        tft.fillScreen(COLOR_BLACK);

        // Лаконичные строгие разделители
        tft.drawFastHLine(15, 62, 290, COLOR_DARK_GRAY);
        tft.drawFastHLine(15, 136, 290, COLOR_DARK_GRAY);
        tft.drawFastHLine(15, 200, 290, COLOR_DARK_GRAY);

        // Заголовок блока MS2
        tft.setFont(&FreeSansBoldOblique9pt7b);
        tft.setTextColor(COLOR_SILVER);
        tft.setCursor(18, 24);
        tft.print("MEGASQUIRT 2 CAN STATUS:");

        // Статус связи с MS2
        tft.setFont(&FreeSansBoldOblique12pt7b);
        tft.setCursor(18, 48);
        if (sens.ms2Online) {
            tft.setTextColor(COLOR_WHITE);
            tft.printf("ONLINE (Packets: %lu)", sens.ms2.packetsTotal);
        } else {
            tft.setTextColor(COLOR_MID_GRAY);
            tft.print("OFFLINE (NO CAN)");
        }

        // Блок Bluetooth Low Energy (BLE)
        tft.setFont(&FreeSansBoldOblique9pt7b);
        tft.setTextColor(COLOR_SILVER);
        tft.setCursor(18, 80);
        tft.print("BLUETOOTH LE (BLE):");

        tft.setFont(); // Растровый для четких символов
        tft.setTextColor(COLOR_WHITE);
        tft.setTextSize(1);
        tft.setCursor(18, 94);
        tft.printf("Device: %s  |  Status: %s", 
                   BleMgr.getDeviceName(), 
                   BleMgr.isConnected() ? "[CONNECTED]" : "[ADVERTISING]");

        tft.setCursor(18, 108);
        tft.printf("MAC:    %s", BleMgr.getMacAddress().c_str());

        tft.setCursor(18, 122);
        tft.printf("CAN:    TX: GPIO %d, RX: GPIO %d (500k)", PIN_CAN_TX, PIN_CAN_RX);

        // Статус GPS
        const GpsData& gps = Gps.getData();
        tft.setFont(&FreeSansBoldOblique9pt7b);
        tft.setTextColor(COLOR_SILVER);
        tft.setCursor(18, 154);
        tft.print("GPS NEO-7M RECEIVER:");

        tft.setFont();
        tft.setTextColor(COLOR_WHITE);
        tft.setTextSize(1);
        tft.setCursor(18, 168);
        tft.printf("UART:   RX: GPIO %d, TX: GPIO %d (9600 baud)", PIN_GPS_RX, PIN_GPS_TX);

        tft.setCursor(18, 182);
        tft.printf("Status: %s  |  Sats: %d  |  Speed: %.1f km/h", 
                   gps.hasFix ? "3D-FIX [OK]" : "SEARCHING...", gps.satellites, gps.speed_kmh);

        // Системные данные
        tft.setTextColor(COLOR_SILVER);
        tft.setCursor(18, 212);
        tft.printf("Heap: %d KB  |  Bright: %d/255  |  Batt: %.2fV", 
                   ESP.getFreeHeap() / 1024, Display.getBrightness(), sens.batteryVoltage);

        tft.setCursor(18, 226);
        tft.printf("Web App: ble_app.html (Web Bluetooth NUS)");
    }
}

// -----------------------------------------------------------------------------
// ГЛАВНЫЙ ЦИКЛ ОБНОВЛЕНИЯ UI
// -----------------------------------------------------------------------------
void UiEngine::update() {
    bool full = needsFullRedraw;
    needsFullRedraw = false;

    switch (currentScreen) {
        case ScreenId::CLASSIC_CLOCK:
            drawClassicClockScreen(full);
            break;
        case ScreenId::OBC_TRIP_FUEL:
            drawObcTripFuelScreen(full);
            break;
        case ScreenId::OBC_TELEMETRY:
            drawObcTelemetryScreen(full);
            break;
        case ScreenId::M_PERFORMANCE:
            drawMPerformanceScreen(full);
            break;
        case ScreenId::SETTINGS_INFO:
            drawSettingsInfoScreen(full);
            break;
        default:
            break;
    }
}
