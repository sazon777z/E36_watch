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
      stopwatchElapsedMillis(0),
      lastGpsSats(-1),
      lastGpsFix(false) {
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

        // Крупные подписи нижних блоков телеметрии (жирный наклонный)
        tft.setFont(&FreeSansBoldOblique12pt7b);
        tft.setTextColor(COLOR_SILVER);
        tft.setCursor(18, 155);
        tft.print("BOOST");
        tft.setCursor(120, 155);
        tft.print("COOLANT");
        tft.setCursor(220, 155);
        tft.print("LAMBDA");

        lastHour = -1;
        lastMinute = -1;
        lastSecond = -1;
    }

    // Мигание наклонного двоеточия раз в секунду (чистый белый)
    if (millis() - lastColonBlinkMillis >= 500) {
        colonState = !colonState;
        lastColonBlinkMillis = millis();
        BmwAssets::drawSlantedColon(tft, 98, 36, colonState, COLOR_WHITE, COLOR_GHOST_SEG, 4, 12);
        BmwAssets::drawSlantedColon(tft, 194, 46, colonState, COLOR_WHITE, COLOR_GHOST_SEG, 3, 9);
    }

    // Отрисовка жирных наклонных белых цифр времени
    if (fullRedraw || td.hour != lastHour || td.minute != lastMinute || td.second != lastSecond) {
        char hBuf[3], mBuf[3], sBuf[3];
        snprintf(hBuf, sizeof(hBuf), "%02d", td.hour);
        snprintf(mBuf, sizeof(mBuf), "%02d", td.minute);
        snprintf(sBuf, sizeof(sBuf), "%02d", td.second);

        // ЧАСЫ (размер 4, наклон 12px, чистый белый цвет)
        BmwAssets::drawSlantedBoldDigit(tft, 20, 36, hBuf[0], COLOR_WHITE, COLOR_GHOST_SEG, 4, 12);
        BmwAssets::drawSlantedBoldDigit(tft, 58, 36, hBuf[1], COLOR_WHITE, COLOR_GHOST_SEG, 4, 12);

        // МИНУТЫ (размер 4, наклон 12px, чистый белый цвет)
        BmwAssets::drawSlantedBoldDigit(tft, 118, 36, mBuf[0], COLOR_WHITE, COLOR_GHOST_SEG, 4, 12);
        BmwAssets::drawSlantedBoldDigit(tft, 156, 36, mBuf[1], COLOR_WHITE, COLOR_GHOST_SEG, 4, 12);

        // СЕКУНДЫ (размер 3, наклон 9px, серебристо-белый цвет)
        BmwAssets::drawSlantedBoldDigit(tft, 210, 46, sBuf[0], COLOR_SILVER, COLOR_GHOST_SEG, 3, 9);
        BmwAssets::drawSlantedBoldDigit(tft, 238, 46, sBuf[1], COLOR_SILVER, COLOR_GHOST_SEG, 3, 9);

        lastHour = td.hour;
        lastMinute = td.minute;
        lastSecond = td.second;
    }

    // Обновление нижних значений телеметрии крупным жирным шрифтом FreeSansBoldOblique18pt7b
    tft.setFont(&FreeSansBoldOblique18pt7b);
    tft.setTextColor(COLOR_WHITE);

    // 1. Наддув (BOOST)
    tft.fillRect(18, 172, 98, 42, COLOR_BLACK);
    tft.setCursor(18, 206);
    if (sens.ms2Online) {
        tft.printf("%+.2fb", sens.ms2.boost_bar);
    } else {
        tft.print("0.00b");
    }

    // 2. Температура ОЖ (COOLANT)
    tft.fillRect(120, 172, 95, 42, COLOR_BLACK);
    tft.setCursor(120, 206);
    tft.printf("%+.0fC", sens.tempOutdoor);

    // 3. Смесь AFR / LAMBDA
    tft.fillRect(220, 172, 98, 42, COLOR_BLACK);
    tft.setCursor(220, 206);
    if (sens.ms2Online) {
        tft.printf("%.1f", sens.ms2.afr);
    } else {
        tft.printf("%.1fV", sens.batteryVoltage);
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
    }

    // 1. СКОРОСТЬ GPS (Крупные жирные наклонные цифры)
    char spdBuf[5];
    int spd = (int)trip.current_speed_kmh;
    if (spd > 999) spd = 999;
    snprintf(spdBuf, sizeof(spdBuf), "%3d", spd);

    // Отрисовка 3 цифр скорости (X = 14, 52, 90, Y = 18)
    BmwAssets::drawSlantedBoldDigit(tft, 14, 18, spdBuf[0], COLOR_WHITE, COLOR_GHOST_SEG, 4, 12);
    BmwAssets::drawSlantedBoldDigit(tft, 52, 18, spdBuf[1], COLOR_WHITE, COLOR_GHOST_SEG, 4, 12);
    BmwAssets::drawSlantedBoldDigit(tft, 90, 18, spdBuf[2], COLOR_WHITE, COLOR_GHOST_SEG, 4, 12);

    // Подпись KM/H
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

    // 2. МГНОВЕННЫЙ РАСХОД (справа от спидометра, X = 170..252)
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

    // 3. КВАДРАНТ 1: Суточный пробег (Trip Distance) - крупным 18pt
    tft.fillRect(16, 114, 138, 38, COLOR_BLACK);
    tft.setFont(&FreeSansBoldOblique18pt7b);
    tft.setTextColor(COLOR_WHITE);
    tft.setCursor(16, 144);
    tft.printf("%.1f km", trip.trip_distance_km);

    // 4. КВАДРАНТ 2: Средний расход (Avg Consumption) - крупным 18pt
    tft.fillRect(168, 114, 148, 38, COLOR_BLACK);
    tft.setCursor(168, 144);
    if (trip.trip_distance_km >= 0.1f) {
        tft.printf("%.1f L", trip.avg_consumption_l_100km);
    } else {
        tft.print("--- L");
    }

    // 5. КВАДРАНТ 3: Общий одометр (Total Odometer) - крупным 18pt
    tft.fillRect(16, 186, 138, 38, COLOR_BLACK);
    tft.setCursor(16, 216);
    tft.printf("%.0f km", trip.total_odometer_km);

    // 6. КВАДРАНТ 4: Израсходовано топлива за поездку - крупным 18pt
    tft.fillRect(168, 186, 148, 38, COLOR_BLACK);
    tft.setCursor(168, 216);
    tft.printf("%.1f L", trip.trip_fuel_liters);

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
    }

    // 1. НАПРЯЖЕНИЕ АКБ (Верх-лево)
    tft.fillRect(15, 30, 138, 50, COLOR_BLACK);
    tft.setFont(&FreeSansBoldOblique18pt7b);
    tft.setTextColor(COLOR_WHITE);
    tft.setCursor(15, 56);
    tft.printf("%.2fV", sens.batteryVoltage);

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

    // 2. ТЕМПЕРАТУРА ОЖ ДВС (Верх-право)
    tft.fillRect(168, 30, 86, 50, COLOR_BLACK);
    tft.setFont(&FreeSansBoldOblique18pt7b);
    tft.setTextColor(COLOR_WHITE);
    tft.setCursor(168, 56);
    tft.printf("%+.0f C", sens.tempOutdoor);

    tft.setFont(&FreeSansBoldOblique9pt7b);
    tft.setTextColor(COLOR_SILVER);
    tft.setCursor(168, 74);
    tft.printf("INTAKE: %+.0f C", sens.tempCabin);

    // 3. СМЕСЬ AFR / ЛЯМБДА (Низ-лево)
    tft.fillRect(15, 110, 138, 50, COLOR_BLACK);
    tft.setFont(&FreeSansBoldOblique18pt7b);
    tft.setTextColor(COLOR_WHITE);
    tft.setCursor(15, 136);
    if (sens.ms2Online) {
        tft.printf("%.1f", sens.ms2.afr);
    } else {
        tft.print("--.-");
    }

    tft.setFont(&FreeSansBoldOblique9pt7b);
    tft.setTextColor(COLOR_SILVER);
    tft.setCursor(15, 154);
    if (sens.ms2Online) {
        tft.printf("TARGET: %.1f", sens.ms2.afr_target);
    } else {
        tft.print("NO CAN DATA");
    }

    // 4. ДРОССЕЛЬ TPS (Низ-право)
    tft.fillRect(168, 110, 140, 50, COLOR_BLACK);
    tft.setFont(&FreeSansBoldOblique18pt7b);
    tft.setTextColor(COLOR_WHITE);
    tft.setCursor(168, 136);
    if (sens.ms2Online) {
        tft.printf("%.0f%%", sens.ms2.tps_pct);
    } else {
        tft.print("--%");
    }

    tft.setFont(&FreeSansBoldOblique9pt7b);
    tft.setTextColor(COLOR_SILVER);
    tft.setCursor(168, 154);
    if (sens.ms2Online) {
        tft.printf("MAP: %.0f kPa", sens.ms2.map_kpa);
    } else {
        tft.print("TPS NO CAN");
    }

    // 5. НИЖНЯЯ ШКАЛА НАДДУВА (BOOST / MAP BAR)
    tft.fillRect(15, 170, 290, 62, COLOR_BLACK);

    tft.setFont(&FreeSansBoldOblique9pt7b);
    tft.setTextColor(COLOR_SILVER);
    tft.setCursor(18, 188);
    tft.print("BOOST:");

    tft.setFont(&FreeSansBoldOblique12pt7b);
    tft.setTextColor(COLOR_WHITE);
    tft.setCursor(88, 188);
    if (sens.ms2Online) {
        tft.printf("%+.2f Bar (%d kPa)", sens.ms2.boost_bar, (int)sens.ms2.map_kpa);
    } else {
        tft.print("0.00 Bar (100 kPa)");
    }

    // Полоса наддува (-0.8 до +1.5 бар)
    tft.drawRect(18, 198, 284, 20, COLOR_DARK_GRAY);
    float boostVal = sens.ms2Online ? sens.ms2.boost_bar : 0.0f;
    float clampedBoost = constrain(boostVal, -0.8f, 1.5f);
    int boostW = (int)((clampedBoost + 0.8f) / 2.3f * 280.0f);
    if (boostW > 0) {
        tft.fillRect(20, 200, boostW, 16, COLOR_WHITE);
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
        tft.setFont();
    }

    if (sens.ms2Online) {
        // Отрисовка жирных наклонных цифр оборотов двигателя (drawSlantedBoldDigit)
        char rpmBuf[6];
        snprintf(rpmBuf, sizeof(rpmBuf), "%4d", sens.ms2.rpm);

        BmwAssets::drawSlantedBoldDigit(tft, 18, 18, rpmBuf[0], COLOR_WHITE, COLOR_GHOST_SEG, 4, 12);
        BmwAssets::drawSlantedBoldDigit(tft, 56, 18, rpmBuf[1], COLOR_WHITE, COLOR_GHOST_SEG, 4, 12);
        BmwAssets::drawSlantedBoldDigit(tft, 94, 18, rpmBuf[2], COLOR_WHITE, COLOR_GHOST_SEG, 4, 12);
        BmwAssets::drawSlantedBoldDigit(tft, 132, 18, rpmBuf[3], COLOR_WHITE, COLOR_GHOST_SEG, 4, 12);

        // Подпись RPM жирным наклонным шрифтом
        tft.setFont(&FreeSansBoldOblique18pt7b);
        tft.setTextColor(COLOR_WHITE);
        tft.setCursor(185, 60);
        tft.print("RPM");

        // Прогрессивная полоса тахометра (Shift-Bar, 0 - 7500 RPM)
        tft.drawRect(18, 88, 284, 24, COLOR_DARK_GRAY);
        int rpmW = map(constrain((int)sens.ms2.rpm, 0, 7500), 0, 7500, 0, 280);
        
        tft.fillRect(20, 90, 280, 20, COLOR_BLACK);
        if (rpmW > 0) {
            uint16_t barCol = (sens.ms2.rpm >= 6500) ? COLOR_BMW_M_RED : COLOR_WHITE;
            tft.fillRect(20, 90, rpmW, 20, barCol);
        }

        // Нижние спортивные показатели (Наддув, УОЗ) шрифтом 18pt
        tft.setFont(&FreeSansBoldOblique18pt7b);
        tft.setTextColor(COLOR_WHITE);

        tft.fillRect(20, 178, 136, 46, COLOR_BLACK);
        tft.setCursor(20, 210);
        tft.printf("%+.2fb", sens.ms2.boost_bar);

        tft.fillRect(168, 178, 136, 46, COLOR_BLACK);
        tft.setCursor(168, 210);
        tft.printf("%.1f*", sens.ms2.advance_deg);

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

        tft.fillRect(25, 40, 270, 50, COLOR_BLACK);
        tft.setFont(&FreeSansBoldOblique18pt7b);
        tft.setTextColor(COLOR_WHITE);
        tft.setCursor(45, 78);
        tft.print(timeBuf);

        tft.fillRect(28, 96, 250, 26, COLOR_BLACK);
        tft.setFont(&FreeSansBoldOblique12pt7b);
        tft.setTextColor(stopwatchRunning ? COLOR_WHITE : COLOR_SILVER);
        tft.setCursor(35, 118);
        tft.print(stopwatchRunning ? ">> TIMING..." : "[ READY ]");
        tft.setFont();
    }

    // Крупный индикатор спутников в правом верхнем углу
    drawGpsCornerIndicator(fullRedraw);
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
