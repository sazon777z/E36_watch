#include "ui_engine.h"
#include "ble_manager.h"
#include "gps_driver.h"
#include "trip_computer.h"
#include <Fonts/FreeSansBoldOblique9pt7b.h>
#include <Fonts/FreeSansBoldOblique12pt7b.h>
#include <Fonts/FreeSansBoldOblique18pt7b.h>

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
      lastColonBlinkMillis(0),
      stopwatchRunning(false),
      stopwatchStartMillis(0),
      stopwatchElapsedMillis(0) {
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
    tft.setTextColor(COLOR_MID_GRAY);
    tft.setCursor(110, 210);
    tft.print("E36 OBC v2.1 BLE");

    // Плавный розжиг подсветки
    Display.fadeIn(DEFAULT_BRIGHTNESS, 4);
    delay(800);
    Display.fadeOut(3);
    
    tft.fillScreen(COLOR_BLACK);
    setScreen(ScreenId::CLASSIC_CLOCK);
    Display.fadeIn(DEFAULT_BRIGHTNESS, 3);
}

void UiEngine::drawHeader(const char* title, bool showStatusIcons) {
    Adafruit_ST7789& tft = Display.getTft();
    tft.setFont(); // Стандартный растровый шрифт для компактной строки статуса
    
    // Верхняя панель на чистом черном фоне
    tft.fillRect(0, 0, SCREEN_WIDTH, 23, COLOR_BLACK);
    tft.drawFastHLine(0, 23, SCREEN_WIDTH, COLOR_DARK_GRAY);

    if (showStatusIcons) {
        const SensorData& sens = Sensors.getData();
        const GpsData& gps = Gps.getData();

        // 1. Иконка АКБ и напряжение (чистый белый)
        BmwAssets::drawBattery(tft, 6, 5, COLOR_WHITE, sens.batteryVoltage);
        tft.setTextColor(COLOR_WHITE);
        tft.setTextSize(1);
        tft.setCursor(32, 7);
        tft.printf("%.1fV", sens.batteryVoltage);

        // 2. Индикатор связи с ЭБУ MegaSquirt 2
        tft.setCursor(70, 7);
        if (sens.ms2Online) {
            tft.setTextColor(COLOR_WHITE);
            tft.print("[MS2]");
        } else {
            tft.setTextColor(COLOR_MID_GRAY);
            tft.print("[MS2:OFF]");
        }

        // 3. Индикатор спутника GPS
        BmwAssets::drawSatellite(tft, 126, 4, COLOR_WHITE, gps.hasFix);
        tft.setCursor(144, 7);
        if (gps.hasFix) {
            tft.setTextColor(COLOR_WHITE);
            tft.printf("%d", gps.satellites);
        } else {
            tft.setTextColor(COLOR_MID_GRAY);
            tft.print("--");
        }

        // 4. Температура ОЖ мотора (CLT) из MS2
        BmwAssets::drawThermometer(tft, 224, 5, COLOR_WHITE);
        tft.setTextColor(COLOR_WHITE);
        tft.setCursor(238, 7);
        tft.printf("%+.0fC", sens.tempOutdoor);

        // 5. Статус Bluetooth Low Energy (BLE)
        bool bleOk = BleMgr.isConnected();
        BmwAssets::drawBluetooth(tft, 298, 4, COLOR_WHITE, bleOk);
    }

    // Заголовок по центру (если статус-иконки скрыты)
    if (!showStatusIcons && title) {
        tft.setTextColor(COLOR_SILVER);
        tft.setTextSize(1);
        int16_t xCenter = (SCREEN_WIDTH - (strlen(title) * 6)) / 2;
        tft.setCursor(xCenter, 7);
        tft.print(title);
    }
}

void UiEngine::drawFooter(const char* leftText, const char* rightText) {
    Adafruit_ST7789& tft = Display.getTft();
    tft.setFont();
    tft.drawFastHLine(0, SCREEN_HEIGHT - 20, SCREEN_WIDTH, COLOR_DARK_GRAY);
    tft.fillRect(0, SCREEN_HEIGHT - 19, SCREEN_WIDTH, 19, COLOR_BLACK);

    tft.setTextColor(COLOR_SILVER);
    tft.setTextSize(1);
    if (leftText) {
        tft.setCursor(8, SCREEN_HEIGHT - 13);
        tft.print(leftText);
    }
    if (rightText) {
        int16_t xRight = SCREEN_WIDTH - (strlen(rightText) * 6) - 8;
        tft.setCursor(xRight, SCREEN_HEIGHT - 13);
        tft.print(rightText);
    }
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
        drawHeader("BMW E36 DIGITAL", true);

        // Тонкая разделительная линия перед блоками телеметрии
        tft.drawFastHLine(15, 158, 290, COLOR_DARK_GRAY);

        // Подписи нижних блоков телеметрии (жирный наклонный)
        tft.setFont(&FreeSansBoldOblique9pt7b);
        tft.setTextColor(COLOR_SILVER);
        tft.setCursor(20, 180);
        tft.print("BOOST");
        tft.setCursor(120, 180);
        tft.print("COOLANT");
        tft.setCursor(220, 180);
        tft.print("LAMBDA");

        drawFooter("E36 DIGITAL CLOCK", "BMW MOTORSPORT");

        lastHour = -1;
        lastMinute = -1;
        lastSecond = -1;
    }

    // Мигание наклонного двоеточия раз в секунду (чистый белый)
    if (millis() - lastColonBlinkMillis >= 500) {
        colonState = !colonState;
        lastColonBlinkMillis = millis();
        BmwAssets::drawSlantedColon(tft, 98, 56, colonState, COLOR_WHITE, COLOR_GHOST_SEG, 4, 12);
        BmwAssets::drawSlantedColon(tft, 194, 66, colonState, COLOR_WHITE, COLOR_GHOST_SEG, 3, 9);
    }

    // Отрисовка жирных наклонных белых цифр времени (центрированы по высоте)
    if (fullRedraw || td.hour != lastHour || td.minute != lastMinute || td.second != lastSecond) {
        char hBuf[3], mBuf[3], sBuf[3];
        snprintf(hBuf, sizeof(hBuf), "%02d", td.hour);
        snprintf(mBuf, sizeof(mBuf), "%02d", td.minute);
        snprintf(sBuf, sizeof(sBuf), "%02d", td.second);

        // ЧАСЫ (размер 4, наклон 12px, чистый белый цвет)
        BmwAssets::drawSlantedBoldDigit(tft, 20, 56, hBuf[0], COLOR_WHITE, COLOR_GHOST_SEG, 4, 12);
        BmwAssets::drawSlantedBoldDigit(tft, 58, 56, hBuf[1], COLOR_WHITE, COLOR_GHOST_SEG, 4, 12);

        // МИНУТЫ (размер 4, наклон 12px, чистый белый цвет)
        BmwAssets::drawSlantedBoldDigit(tft, 118, 56, mBuf[0], COLOR_WHITE, COLOR_GHOST_SEG, 4, 12);
        BmwAssets::drawSlantedBoldDigit(tft, 156, 56, mBuf[1], COLOR_WHITE, COLOR_GHOST_SEG, 4, 12);

        // СЕКУНДЫ (размер 3, наклон 9px, серебристо-белый цвет)
        BmwAssets::drawSlantedBoldDigit(tft, 210, 66, sBuf[0], COLOR_SILVER, COLOR_GHOST_SEG, 3, 9);
        BmwAssets::drawSlantedBoldDigit(tft, 238, 66, sBuf[1], COLOR_SILVER, COLOR_GHOST_SEG, 3, 9);

        lastHour = td.hour;
        lastMinute = td.minute;
        lastSecond = td.second;
    }

    // Обновление нижних значений телеметрии (жирные белые цифры)
    tft.setFont(&FreeSansBoldOblique12pt7b);
    tft.setTextColor(COLOR_WHITE);

    // 1. Наддув (BOOST)
    tft.fillRect(20, 186, 90, 24, COLOR_BLACK);
    tft.setCursor(20, 205);
    if (sens.ms2Online) {
        tft.printf("%+.2fb", sens.ms2.boost_bar);
    } else {
        tft.print("0.00b");
    }

    // 2. Температура ОЖ (COOLANT)
    tft.fillRect(120, 186, 85, 24, COLOR_BLACK);
    tft.setCursor(120, 205);
    tft.printf("%+.0fC", sens.tempOutdoor);

    // 3. Смесь AFR / LAMBDA
    tft.fillRect(220, 186, 90, 24, COLOR_BLACK);
    tft.setCursor(220, 205);
    if (sens.ms2Online) {
        tft.printf("%.1f", sens.ms2.afr);
    } else {
        tft.printf("%.1fV", sens.batteryVoltage);
    }

    tft.setFont(); // Сброс шрифта
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
        drawHeader("OBC TRIP & FUEL", true);

        // Горизонтальные разделители
        tft.drawFastHLine(10, 108, 300, COLOR_DARK_GRAY);
        tft.drawFastHLine(10, 166, 300, COLOR_DARK_GRAY);
        tft.drawFastVLine(158, 114, 98, COLOR_DARK_GRAY);

        // Статические подписи нижних квадрантов (жирный наклонный)
        tft.setFont(&FreeSansBoldOblique9pt7b);
        tft.setTextColor(COLOR_SILVER);

        tft.setCursor(16, 126);
        tft.print("TRIP DISTANCE:");

        tft.setCursor(168, 126);
        tft.print("AVG CONSUMPTION:");

        tft.setCursor(16, 182);
        tft.print("TOTAL ODOMETER:");

        tft.setCursor(168, 182);
        tft.print("FUEL / AVG SPEED:");

        drawFooter("HOLD: RESET TRIP", "GPS NEO-7M & MS2");
    }

    // 1. СКОРОСТЬ GPS (Крупные жирные наклонные цифры)
    char spdBuf[5];
    int spd = (int)trip.current_speed_kmh;
    if (spd > 999) spd = 999;
    snprintf(spdBuf, sizeof(spdBuf), "%3d", spd);

    // Отрисовка 3 цифр скорости (X = 14, 52, 90, Y = 32)
    BmwAssets::drawSlantedBoldDigit(tft, 14, 32, spdBuf[0], COLOR_WHITE, COLOR_GHOST_SEG, 4, 12);
    BmwAssets::drawSlantedBoldDigit(tft, 52, 32, spdBuf[1], COLOR_WHITE, COLOR_GHOST_SEG, 4, 12);
    BmwAssets::drawSlantedBoldDigit(tft, 90, 32, spdBuf[2], COLOR_WHITE, COLOR_GHOST_SEG, 4, 12);

    // Подпись KM/H
    tft.setFont(&FreeSansBoldOblique9pt7b);
    tft.setTextColor(COLOR_SILVER);
    tft.setCursor(132, 55);
    tft.print("KM/H");
    if (!gps.hasFix) {
        tft.setTextColor(COLOR_MID_GRAY);
        tft.setCursor(130, 80);
        tft.print("NO FIX");
    } else {
        tft.fillRect(130, 68, 40, 20, COLOR_BLACK);
    }

    // 2. МГНОВЕННЫЙ РАСХОД (справа от спидометра)
    tft.fillRect(174, 28, 142, 76, COLOR_BLACK);
    tft.setCursor(176, 44);
    tft.setTextColor(COLOR_SILVER);
    tft.setFont(&FreeSansBoldOblique9pt7b);
    tft.print(trip.isLitersPerHour ? "INSTANT (L/H):" : "INSTANT (L/100):");

    tft.setFont(&FreeSansBoldOblique18pt7b);
    tft.setTextColor(COLOR_WHITE);
    tft.setCursor(176, 85);
    tft.printf("%.1f", trip.instant_consumption);

    // 3. КВАДРАНТ 1: Суточный пробег (Trip Distance)
    tft.fillRect(16, 132, 138, 28, COLOR_BLACK);
    tft.setFont(&FreeSansBoldOblique12pt7b);
    tft.setTextColor(COLOR_WHITE);
    tft.setCursor(16, 154);
    tft.printf("%.1f km", trip.trip_distance_km);

    // 4. КВАДРАНТ 2: Средний расход (Avg Consumption)
    tft.fillRect(168, 132, 148, 28, COLOR_BLACK);
    tft.setCursor(168, 154);
    if (trip.trip_distance_km >= 0.1f) {
        tft.printf("%.1f L", trip.avg_consumption_l_100km);
    } else {
        tft.print("--- L");
    }

    // 5. КВАДРАНТ 3: Общий одометр (Total Odometer)
    tft.fillRect(16, 188, 138, 26, COLOR_BLACK);
    tft.setCursor(16, 207);
    tft.printf("%.0f km", trip.total_odometer_km);

    // 6. КВАДРАНТ 4: Израсходовано топлива / Средняя скорость
    tft.fillRect(168, 188, 148, 26, COLOR_BLACK);
    tft.setCursor(168, 207);
    tft.printf("%.1fL / %.0f", trip.trip_fuel_liters, trip.avg_speed_kmh);

    tft.setFont(); // Сброс шрифта
}

// -----------------------------------------------------------------------------
// ЭКРАН 3: ТЕЛЕМЕТРИЯ И БОРТОВОЙ КОМПЬЮТЕР (OBC TELEMETRY & ENGINE)
// -----------------------------------------------------------------------------
void UiEngine::drawObcTelemetryScreen(bool fullRedraw) {
    Adafruit_ST7789& tft = Display.getTft();
    const SensorData& sens = Sensors.getData();

    if (fullRedraw) {
        tft.fillScreen(COLOR_BLACK);
        drawHeader("ENGINE TELEMETRY", true);

        // Тонкие разделительные линии координатной сетки (1 px)
        tft.drawFastHLine(10, 102, 300, COLOR_DARK_GRAY);
        tft.drawFastHLine(10, 172, 300, COLOR_DARK_GRAY);
        tft.drawFastVLine(158, 30, 142, COLOR_DARK_GRAY);

        // Статические подписи параметров (FreeSansBoldOblique9pt7b)
        tft.setFont(&FreeSansBoldOblique9pt7b);
        tft.setTextColor(COLOR_SILVER);
        tft.setCursor(15, 46);  tft.print("BATTERY");
        tft.setCursor(168, 46); tft.print("COOLANT (CLT)");
        tft.setCursor(15, 118); tft.print("AIR / FUEL (AFR)");
        tft.setCursor(168, 118);tft.print("THROTTLE (TPS)");

        tft.setFont(); // Сброс
        drawFooter("E36 LIVE TELEMETRY", sens.ms2Online ? "MS2 500K CAN" : "MS2 OFFLINE");
    }

    // 1. НАПРЯЖЕНИЕ АКБ (Верх-лево)
    tft.fillRect(15, 52, 138, 46, COLOR_BLACK);
    tft.setFont(&FreeSansBoldOblique18pt7b);
    tft.setTextColor(COLOR_WHITE);
    tft.setCursor(15, 80);
    tft.printf("%.2fV", sens.batteryVoltage);

    tft.setFont(&FreeSansBoldOblique9pt7b);
    tft.setTextColor(COLOR_SILVER);
    tft.setCursor(15, 96);
    if (sens.voltStatus == VoltageStatus::VOLT_CRITICAL_LOW) {
        tft.setTextColor(COLOR_STATUS_ERR);
        tft.print("LOW BATTERY!");
    } else if (sens.voltStatus == VoltageStatus::VOLT_NORMAL_RUNNING) {
        tft.print("ALT: CHARGING");
    } else {
        tft.printf("CRANK: %.1fV", sens.minCrankVoltage);
    }

    // 2. ТЕМПЕРАТУРА ОЖ ДВС (Верх-право)
    tft.fillRect(168, 52, 140, 46, COLOR_BLACK);
    tft.setFont(&FreeSansBoldOblique18pt7b);
    tft.setTextColor(COLOR_WHITE);
    tft.setCursor(168, 80);
    tft.printf("%+.0f C", sens.tempOutdoor);

    tft.setFont(&FreeSansBoldOblique9pt7b);
    tft.setTextColor(COLOR_SILVER);
    tft.setCursor(168, 96);
    tft.printf("INTAKE: %+.0f C", sens.tempCabin);

    // 3. СМЕСЬ AFR / ЛЯМБДА (Низ-лево)
    tft.fillRect(15, 124, 138, 46, COLOR_BLACK);
    tft.setFont(&FreeSansBoldOblique18pt7b);
    tft.setTextColor(COLOR_WHITE);
    tft.setCursor(15, 152);
    if (sens.ms2Online) {
        tft.printf("%.1f", sens.ms2.afr);
    } else {
        tft.print("--.-");
    }

    tft.setFont(&FreeSansBoldOblique9pt7b);
    tft.setTextColor(COLOR_SILVER);
    tft.setCursor(15, 168);
    if (sens.ms2Online) {
        tft.printf("TARGET: %.1f", sens.ms2.afr_target);
    } else {
        tft.print("AFR NO CAN");
    }

    // 4. ДРОССЕЛЬ TPS (Низ-право)
    tft.fillRect(168, 124, 140, 46, COLOR_BLACK);
    tft.setFont(&FreeSansBoldOblique18pt7b);
    tft.setTextColor(COLOR_WHITE);
    tft.setCursor(168, 152);
    if (sens.ms2Online) {
        tft.printf("%.0f%%", sens.ms2.tps_pct);
    } else {
        tft.print("--%");
    }

    tft.setFont(&FreeSansBoldOblique9pt7b);
    tft.setTextColor(COLOR_SILVER);
    tft.setCursor(168, 168);
    if (sens.ms2Online) {
        tft.printf("MAP: %.0f kPa", sens.ms2.map_kpa);
    } else {
        tft.print("TPS NO CAN");
    }

    // 5. НИЖНЯЯ ШКАЛА НАДДУВА (BOOST / MAP BAR)
    tft.setFont(); // Стандартный растровый
    tft.fillRect(15, 178, 290, 36, COLOR_BLACK);

    tft.setTextColor(COLOR_SILVER);
    tft.setTextSize(1);
    tft.setCursor(18, 180);
    tft.print("MANIFOLD PRESSURE (BOOST)");

    tft.setTextColor(COLOR_WHITE);
    tft.setCursor(195, 180);
    if (sens.ms2Online) {
        tft.printf("%+.2f Bar (%d kPa)", sens.ms2.boost_bar, (int)sens.ms2.map_kpa);
    } else {
        tft.print("0.00 Bar (100 kPa)");
    }

    // Минималистичная полоса наддува (-0.8 до +1.5 бар)
    tft.drawRect(18, 194, 284, 12, COLOR_DARK_GRAY);
    float boostVal = sens.ms2Online ? sens.ms2.boost_bar : 0.0f;
    float clampedBoost = constrain(boostVal, -0.8f, 1.5f);
    int boostW = (int)((clampedBoost + 0.8f) / 2.3f * 282.0f);
    if (boostW > 0) {
        tft.fillRect(19, 195, boostW, 10, COLOR_WHITE);
    }
}

// -----------------------------------------------------------------------------
// ЭКРАН 3: СПОРТИВНЫЙ ЭКРАН ///M PERFORMANCE & ТАХОМЕТР
// -----------------------------------------------------------------------------
void UiEngine::drawMPerformanceScreen(bool fullRedraw) {
    Adafruit_ST7789& tft = Display.getTft();
    const SensorData& sens = Sensors.getData();

    if (fullRedraw) {
        tft.fillScreen(COLOR_BLACK);
        
        // Лаконичный заголовок ///M Motorsport
        tft.setFont(&FreeSansBoldOblique12pt7b);
        tft.setTextColor(COLOR_WHITE);
        tft.setCursor(15, 22);
        tft.print("///M MOTORSPORT");

        tft.setFont(); // Сброс
        tft.drawFastHLine(0, 26, SCREEN_WIDTH, COLOR_DARK_GRAY);

        // Статические метки шкалы оборотов (0 .. 7.5k)
        tft.setTextColor(COLOR_SILVER);
        tft.setTextSize(1);
        tft.setCursor(20, 144);  tft.print("0");
        tft.setCursor(85, 144);  tft.print("2k");
        tft.setCursor(150, 144); tft.print("4k");
        tft.setCursor(215, 144); tft.print("6k");
        tft.setCursor(275, 144); tft.print("7.5k");

        // Тонкая разделительная линия перед нижними параметрами
        tft.drawFastHLine(15, 164, 290, COLOR_DARK_GRAY);

        drawFooter("M-PERFORMANCE DASH", sens.ms2Online ? "LIVE RPM / BOOST" : "STOPWATCH");
    }

    if (sens.ms2Online) {
        // Отрисовка жирных наклонных цифр оборотов двигателя (drawSlantedBoldDigit)
        char rpmBuf[6];
        snprintf(rpmBuf, sizeof(rpmBuf), "%4d", sens.ms2.rpm);

        BmwAssets::drawSlantedBoldDigit(tft, 18, 42, rpmBuf[0], COLOR_WHITE, COLOR_GHOST_SEG, 4, 12);
        BmwAssets::drawSlantedBoldDigit(tft, 56, 42, rpmBuf[1], COLOR_WHITE, COLOR_GHOST_SEG, 4, 12);
        BmwAssets::drawSlantedBoldDigit(tft, 94, 42, rpmBuf[2], COLOR_WHITE, COLOR_GHOST_SEG, 4, 12);
        BmwAssets::drawSlantedBoldDigit(tft, 132, 42, rpmBuf[3], COLOR_WHITE, COLOR_GHOST_SEG, 4, 12);

        // Подпись RPM жирным наклонным шрифтом
        tft.setFont(&FreeSansBoldOblique18pt7b);
        tft.setTextColor(COLOR_WHITE);
        tft.setCursor(185, 84);
        tft.print("RPM");

        // Прогрессивная полоса тахометра (Shift-Bar, 0 - 7500 RPM)
        tft.drawRect(18, 120, 284, 18, COLOR_DARK_GRAY);
        int rpmW = map(constrain((int)sens.ms2.rpm, 0, 7500), 0, 7500, 0, 280);
        
        tft.fillRect(20, 122, 280, 14, COLOR_BLACK);
        if (rpmW > 0) {
            uint16_t barCol = (sens.ms2.rpm >= 6500) ? COLOR_BMW_M_RED : COLOR_WHITE;
            tft.fillRect(20, 122, rpmW, 14, barCol);
        }

        // Нижние спортивные показатели (Наддув, УОЗ)
        tft.setFont(&FreeSansBoldOblique12pt7b);
        tft.setTextColor(COLOR_WHITE);

        tft.fillRect(15, 172, 290, 24, COLOR_BLACK);
        tft.setCursor(15, 192);
        tft.printf("BAR: %+.2f", sens.ms2.boost_bar);

        tft.setCursor(150, 192);
        tft.printf("ADV: %.1f", sens.ms2.advance_deg);

        tft.setFont(); // Сброс

    } else {
        // Резервный секундомер (если MS2 оффлайн)
        unsigned long currentElapsed = stopwatchElapsedMillis;
        if (stopwatchRunning) {
            currentElapsed += (millis() - stopwatchStartMillis);
        }
        unsigned long totalSec = currentElapsed / 1000;
        unsigned long msFraction = (currentElapsed % 1000) / 100;
        int m = totalSec / 60;
        int s = totalSec % 60;

        char timeBuf[12];
        snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d.%1d", m, s, (int)msFraction);

        tft.fillRect(25, 52, 270, 50, COLOR_BLACK);
        tft.setFont(&FreeSansBoldOblique18pt7b);
        tft.setTextColor(COLOR_WHITE);
        tft.setCursor(45, 90);
        tft.print(timeBuf);

        tft.fillRect(28, 120, 250, 18, COLOR_BLACK);
        tft.setFont(&FreeSansBoldOblique9pt7b);
        tft.setTextColor(stopwatchRunning ? COLOR_WHITE : COLOR_SILVER);
        tft.setCursor(35, 134);
        tft.print(stopwatchRunning ? ">> TIMING..." : "[ READY ]");
        tft.setFont();
    }
}

void UiEngine::toggleStopwatch() {
    if (stopwatchRunning) {
        stopwatchElapsedMillis += (millis() - stopwatchStartMillis);
        stopwatchRunning = false;
    } else {
        stopwatchStartMillis = millis();
        stopwatchRunning = true;
    }
}

void UiEngine::resetStopwatch() {
    stopwatchRunning = false;
    stopwatchStartMillis = 0;
    stopwatchElapsedMillis = 0;
    needsFullRedraw = true;
}

// -----------------------------------------------------------------------------
// ЭКРАН 4: ИНФОРМАЦИЯ И НАСТРОЙКИ WI-FI ПОРТАЛА
// -----------------------------------------------------------------------------
void UiEngine::drawSettingsInfoScreen(bool fullRedraw) {
    Adafruit_ST7789& tft = Display.getTft();
    const SensorData& sens = Sensors.getData();

    if (fullRedraw) {
        tft.fillScreen(COLOR_BLACK);
        drawHeader("SETTINGS & DIAG", false);

        // Лаконичные строгие разделители
        tft.drawFastHLine(15, 96, 290, COLOR_DARK_GRAY);
        tft.drawFastHLine(15, 170, 290, COLOR_DARK_GRAY);

        // Заголовок блока MS2
        tft.setFont(&FreeSansBoldOblique9pt7b);
        tft.setTextColor(COLOR_SILVER);
        tft.setCursor(18, 46);
        tft.print("MEGASQUIRT 2 CAN STATUS:");

        // Статус связи с MS2
        tft.setFont(&FreeSansBoldOblique12pt7b);
        tft.setTextColor(COLOR_WHITE);
        tft.setCursor(18, 70);
        if (sens.ms2Online) {
            tft.printf("ONLINE (Packets: %lu)", sens.ms2.packetsTotal);
        } else {
            tft.setTextColor(COLOR_MID_GRAY);
            tft.print("OFFLINE (NO CAN)");
        }

        // Блок Bluetooth Low Energy (BLE)
        tft.setFont(&FreeSansBoldOblique9pt7b);
        tft.setTextColor(COLOR_SILVER);
        tft.setCursor(18, 114);
        tft.print("BLUETOOTH LE (BLE):");

        tft.setFont(); // Растровый для четких символов
        tft.setTextColor(COLOR_WHITE);
        tft.setTextSize(1);
        tft.setCursor(18, 126);
        tft.printf("Device: %s  |  Status: %s", 
                   BleMgr.getDeviceName(), 
                   BleMgr.isConnected() ? "[CONNECTED]" : "[ADVERTISING]");

        tft.setCursor(18, 142);
        tft.printf("MAC:    %s", BleMgr.getMacAddress().c_str());

        tft.setCursor(18, 156);
        tft.printf("CAN:    TX: GPIO %d, RX: GPIO %d (500k)", PIN_CAN_TX, PIN_CAN_RX);

        // Статус GPS
        const GpsData& gps = Gps.getData();
        tft.setCursor(18, 170);
        tft.printf("GPS:    RX: GPIO %d, TX: GPIO %d (Fix: %s, Sats: %d)", 
                   PIN_GPS_RX, PIN_GPS_TX, gps.hasFix ? "3D-OK" : "SEARCH", gps.satellites);

        // Системные данные
        tft.setTextColor(COLOR_SILVER);
        tft.setCursor(18, 186);
        tft.printf("Free Heap: %d KB  |  Brightness: %d/255", ESP.getFreeHeap() / 1024, Display.getBrightness());

        tft.setCursor(18, 200);
        tft.printf("App:    Web Bluetooth (ble_app.html) / NUS App");

        drawFooter("CLICK: NEXT SCREEN", "BMW E36 1991");
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
