#include "ui_engine.h"
#include <WiFi.h>

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
      lastWifiState(false),
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

    // Отрисовка фирменного логотипа BMW в центре
    BmwAssets::drawRoundel(tft, 160, 85, 42);

    // Триколор ///M
    BmwAssets::drawMPowerLogo(tft, 105, 140, 20);

    // Заголовок и подзаголовок
    tft.setTextColor(COLOR_BMW_AMBER_BRIGHT);
    tft.setTextSize(2);
    tft.setCursor(55, 175);
    tft.print("BMW BAVARIA");

    tft.setTextColor(COLOR_BMW_AMBER_DIM);
    tft.setTextSize(1);
    tft.setCursor(85, 202);
    tft.print(subtitle);

    tft.setTextColor(COLOR_MID_GRAY);
    tft.setCursor(120, 220);
    tft.print("E36 OBC v1.0");

    // Плавный розжиг подсветки
    Display.fadeIn(DEFAULT_BRIGHTNESS, 5);
    delay(1600);
    Display.fadeOut(3);
    
    tft.fillScreen(COLOR_BLACK);
    setScreen(ScreenId::CLASSIC_CLOCK);
    Display.fadeIn(DEFAULT_BRIGHTNESS, 3);
}

void UiEngine::drawHeader(const char* title, bool showStatusIcons) {
    Adafruit_ST7789& tft = Display.getTft();
    
    // Верхняя панель
    tft.fillRect(0, 0, SCREEN_WIDTH, 24, COLOR_BLACK);
    tft.drawFastHLine(0, 24, SCREEN_WIDTH, COLOR_DARK_GRAY);

    if (showStatusIcons) {
        const SensorData& sens = Sensors.getData();
        // Иконка АКБ и напряжение
        BmwAssets::drawBattery(tft, 8, 6, COLOR_BMW_AMBER_MAIN, sens.batteryVoltage);
        tft.setTextColor(COLOR_BMW_AMBER_BRIGHT);
        tft.setTextSize(1);
        tft.setCursor(35, 8);
        tft.printf("%.1fV", sens.batteryVoltage);

        // Индикатор связи с ЭБУ MegaSquirt 2
        tft.setCursor(80, 8);
        if (sens.ms2Online) {
            tft.setTextColor(COLOR_STATUS_OK);
            tft.print("MS2:OK");
        } else {
            tft.setTextColor(COLOR_MID_GRAY);
            tft.print("MS2:OFF");
        }

        // Температура ОЖ мотора (CLT) из MS2 или датчика
        BmwAssets::drawThermometer(tft, 218, 5, COLOR_BMW_AMBER_MAIN);
        tft.setTextColor(COLOR_BMW_AMBER_BRIGHT);
        tft.setCursor(232, 8);
        tft.printf("%+.0fC", sens.tempOutdoor);

        // Статус Wi-Fi
        bool wifiOk = (WiFi.status() == WL_CONNECTED);
        BmwAssets::drawWifi(tft, 296, 5, wifiOk ? COLOR_STATUS_OK : COLOR_MID_GRAY, wifiOk);
    }

    // Заголовок по центру
    tft.setTextColor(COLOR_WHITE);
    tft.setTextSize(1);
    int16_t xCenter = (SCREEN_WIDTH - (strlen(title) * 6)) / 2;
    tft.setCursor(xCenter, 8);
    tft.print(title);
}

void UiEngine::drawFooter(const char* leftText, const char* rightText) {
    Adafruit_ST7789& tft = Display.getTft();
    tft.drawFastHLine(0, SCREEN_HEIGHT - 22, SCREEN_WIDTH, COLOR_DARK_GRAY);
    tft.fillRect(0, SCREEN_HEIGHT - 21, SCREEN_WIDTH, 21, COLOR_BLACK);

    tft.setTextColor(COLOR_MID_GRAY);
    tft.setTextSize(1);
    if (leftText) {
        tft.setCursor(8, SCREEN_HEIGHT - 14);
        tft.print(leftText);
    }
    if (rightText) {
        int16_t xRight = SCREEN_WIDTH - (strlen(rightText) * 6) - 8;
        tft.setCursor(xRight, SCREEN_HEIGHT - 14);
        tft.print(rightText);
    }
}

// -----------------------------------------------------------------------------
// ЭКРАН 1: ШТАТНЫЕ ЧАСЫ BMW E36 (CLASSIC CLOCK & DATE)
// -----------------------------------------------------------------------------
void UiEngine::drawClassicClockScreen(bool fullRedraw) {
    Adafruit_ST7789& tft = Display.getTft();
    const TimeData& td = Time.getTime();
    const SensorData& sens = Sensors.getData();

    if (fullRedraw) {
        tft.fillScreen(COLOR_BLACK);
        drawHeader("BMW BAVARIA OBC", true);

        // Рамка блока часов в стиле оригинального дисплея E36
        tft.drawRoundRect(15, 36, 290, 96, 4, COLOR_DARK_GRAY);
        tft.drawRoundRect(16, 37, 288, 94, 3, COLOR_BMW_AMBER_DARK);

        // Нижние кнопки-вкладки в стиле кнопок OBC E36
        // [UHR/DAT] [TEMP] [TIMER] [CHECK]
        const char* obcTabs[] = { "UHR/DAT", "TEMP", "TIMER", "CHECK" };
        for (int i = 0; i < 4; i++) {
            int16_t bx = 15 + i * 73;
            int16_t bw = 70;
            int16_t by = 196;
            int16_t bh = 22;
            
            uint16_t borderCol = (i == 0) ? COLOR_BMW_AMBER_MAIN : COLOR_DARK_GRAY;
            uint16_t textCol = (i == 0) ? COLOR_BMW_AMBER_BRIGHT : COLOR_MID_GRAY;
            
            tft.drawRoundRect(bx, by, bw, bh, 3, borderCol);
            tft.setTextColor(textCol);
            tft.setTextSize(1);
            int16_t tx = bx + (bw - (strlen(obcTabs[i]) * 6)) / 2;
            tft.setCursor(tx, by + 7);
            tft.print(obcTabs[i]);
        }

        lastHour = -1;
        lastMinute = -1;
        lastSecond = -1;
        lastDay = -1;
    }

    // Мигание двоеточия раз в секунду
    if (millis() - lastColonBlinkMillis >= 500) {
        colonState = !colonState;
        lastColonBlinkMillis = millis();
        // Перерисовываем разделительные двоеточия
        BmwAssets::draw7SegmentColon(tft, 102, 54, colonState, COLOR_BMW_AMBER_BRIGHT, COLOR_BMW_AMBER_GRID, 4);
        BmwAssets::draw7SegmentColon(tft, 190, 54, colonState, COLOR_BMW_AMBER_BRIGHT, COLOR_BMW_AMBER_GRID, 4);
    }

    // Отрисовка цифр часов (только при изменении)
    if (fullRedraw || td.hour != lastHour || td.minute != lastMinute || td.second != lastSecond) {
        char hBuf[3], mBuf[3], sBuf[3];
        snprintf(hBuf, sizeof(hBuf), "%02d", td.hour);
        snprintf(mBuf, sizeof(mBuf), "%02d", td.minute);
        snprintf(sBuf, sizeof(sBuf), "%02d", td.second);

        // Часы
        BmwAssets::draw7SegmentDigit(tft, 28, 54, hBuf[0], COLOR_BMW_AMBER_BRIGHT, COLOR_BMW_AMBER_GRID, 4);
        BmwAssets::draw7SegmentDigit(tft, 63, 54, hBuf[1], COLOR_BMW_AMBER_BRIGHT, COLOR_BMW_AMBER_GRID, 4);

        // Минуты
        BmwAssets::draw7SegmentDigit(tft, 116, 54, mBuf[0], COLOR_BMW_AMBER_BRIGHT, COLOR_BMW_AMBER_GRID, 4);
        BmwAssets::draw7SegmentDigit(tft, 151, 54, mBuf[1], COLOR_BMW_AMBER_BRIGHT, COLOR_BMW_AMBER_GRID, 4);

        // Секунды
        BmwAssets::draw7SegmentDigit(tft, 204, 54, sBuf[0], COLOR_BMW_AMBER_MAIN, COLOR_BMW_AMBER_GRID, 4);
        BmwAssets::draw7SegmentDigit(tft, 239, 54, sBuf[1], COLOR_BMW_AMBER_MAIN, COLOR_BMW_AMBER_GRID, 4);

        lastHour = td.hour;
        lastMinute = td.minute;
        lastSecond = td.second;
    }

    // Отрисовка даты и дня недели в центральной полосе
    if (fullRedraw || td.day != lastDay) {
        tft.fillRect(16, 142, 288, 44, COLOR_BLACK);
        tft.drawFastHLine(20, 142, 280, COLOR_DARK_GRAY);

        // День недели и дата
        tft.setTextColor(COLOR_BMW_AMBER_MAIN);
        tft.setTextSize(2);
        tft.setCursor(35, 154);
        tft.printf("%s, %s", td.dayOfWeekStrRu, td.dateStr);

        lastDay = td.day;
    }
}

// -----------------------------------------------------------------------------
// ЭКРАН 2: ТЕЛЕМЕТРИЯ И БОРТОВОЙ КОМПЬЮТЕР (OBC TELEMETRY & BATTERY)
// -----------------------------------------------------------------------------
void UiEngine::drawObcTelemetryScreen(bool fullRedraw) {
    Adafruit_ST7789& tft = Display.getTft();
    const SensorData& sens = Sensors.getData();
    unsigned long uptime = Time.getUptimeSeconds();

    if (fullRedraw) {
        tft.fillScreen(COLOR_BLACK);
        drawHeader("E36 TELEMETRY & DIAG", true);

        // Блок 1: Вольтметр бортсети (слева)
        tft.drawRoundRect(10, 32, 145, 112, 4, COLOR_DARK_GRAY);
        tft.setTextColor(COLOR_MID_GRAY);
        tft.setTextSize(1);
        tft.setCursor(20, 40);
        tft.print("НАПРЯЖЕНИЕ АКБ");

        // Блок 2: Данные двигателя MS2 / Uptime (справа)
        tft.drawRoundRect(165, 32, 145, 112, 4, COLOR_DARK_GRAY);
        tft.setTextColor(COLOR_MID_GRAY);
        tft.setTextSize(1);
        tft.setCursor(175, 40);
        tft.print(sens.ms2Online ? "ДВИГАТЕЛЬ (MS2)" : "ВРЕМЯ ПОЕЗДКИ");

        // Блок 3: Шкала давления MAP / Вольтметр снизу
        tft.drawRoundRect(10, 152, 300, 60, 4, COLOR_DARK_GRAY);
        tft.setTextColor(COLOR_MID_GRAY);
        tft.setCursor(18, 160);
        tft.print(sens.ms2Online ? "ДАВЛЕНИЕ ВПУСКА / НАДДУВ (MAP)" : "МОНИТОР ЗАРЯДА ГЕНЕРАТОРА");

        drawFooter("КНОПКА: СЛЕД. ЭКРАН", sens.ms2Online ? "MS2 500K CAN" : "E36 OBC");
    }

    // 1. Обновление вольтметра
    tft.fillRect(16, 56, 133, 40, COLOR_BLACK);
    uint16_t vCol = COLOR_BMW_AMBER_BRIGHT;
    if (sens.voltStatus == VoltageStatus::VOLT_CRITICAL_LOW || sens.voltStatus == VoltageStatus::VOLT_OVERCHARGE) {
        vCol = COLOR_STATUS_ERR;
    } else if (sens.voltStatus == VoltageStatus::VOLT_NORMAL_RUNNING) {
        vCol = COLOR_STATUS_OK;
    }

    tft.setTextColor(vCol);
    tft.setTextSize(3);
    tft.setCursor(20, 60);
    tft.printf("%.2f", sens.batteryVoltage);
    tft.setTextSize(1);
    tft.setCursor(118, 65);
    tft.print("V");

    // Текстовый статус генератора/АКБ
    tft.fillRect(16, 102, 133, 36, COLOR_BLACK);
    tft.setTextSize(1);
    tft.setTextColor(COLOR_BMW_AMBER_MAIN);
    tft.setCursor(18, 104);
    switch (sens.voltStatus) {
        case VoltageStatus::VOLT_CRITICAL_LOW:
            tft.setTextColor(COLOR_STATUS_ERR);
            tft.print("АКБ РАЗРЯЖЕН!");
            break;
        case VoltageStatus::VOLT_LOW:
            tft.setTextColor(COLOR_STATUS_WARN);
            tft.print("НИЗКИЙ ЗАРЯД");
            break;
        case VoltageStatus::VOLT_NORMAL_REST:
            tft.print("ЗАЖИГАНИЕ ВКЛ");
            break;
        case VoltageStatus::VOLT_NORMAL_RUNNING:
            tft.setTextColor(COLOR_STATUS_OK);
            tft.print("ЗАРЯД ГЕНЕРАТОРА: ОК");
            break;
        case VoltageStatus::VOLT_OVERCHARGE:
            tft.setTextColor(COLOR_STATUS_ERR);
            tft.print("ПЕРЕЗАРЯД >14.8V!");
            break;
    }

    tft.setTextColor(COLOR_MID_GRAY);
    tft.setCursor(18, 122);
    tft.printf("Пуск: %.1fV", sens.minCrankVoltage);

    // 2. Обновление правого блока (MS2 данные или Uptime)
    tft.fillRect(172, 54, 132, 84, COLOR_BLACK);
    if (sens.ms2Online) {
        tft.setTextSize(1);
        tft.setTextColor(COLOR_BMW_AMBER_BRIGHT);
        tft.setCursor(174, 56);
        tft.printf("ОЖ (CLT):  %+.0f C", sens.ms2.clt_c);

        tft.setTextColor(COLOR_BMW_AMBER_MAIN);
        tft.setCursor(174, 74);
        tft.printf("Впуск (IAT): %+.0f C", sens.ms2.mat_c);

        tft.setCursor(174, 92);
        tft.printf("Дроссель:   %.0f %%", sens.ms2.tps_pct);

        tft.setTextColor(COLOR_WHITE);
        tft.setCursor(174, 110);
        tft.printf("Смесь AFR:  %.1f", sens.ms2.afr);
    } else {
        int uH = uptime / 3600;
        int uM = (uptime % 3600) / 60;
        int uS = uptime % 60;

        tft.setTextColor(COLOR_BMW_AMBER_BRIGHT);
        tft.setTextSize(2);
        tft.setCursor(176, 64);
        tft.printf("%02d:%02d:%02d", uH, uM, uS);

        tft.setTextColor(COLOR_BMW_AMBER_MAIN);
        tft.setTextSize(1);
        tft.setCursor(175, 104);
        tft.printf("Салон: %+.1f C", sens.tempCabin);
        tft.setCursor(175, 122);
        tft.printf("Улица: %+.1f C", sens.tempOutdoor);
    }

    // 3. Нижний графический индикатор (MAP наддув или шкала АКБ)
    tft.fillRect(20, 178, 260, 14, COLOR_BLACK);
    tft.drawRect(19, 177, 262, 16, COLOR_DARK_GRAY);

    if (sens.ms2Online) {
        // Шкала давления во впуске (от 20 кПа / -0.80 бар до 250 кПа / +1.50 бар)
        float mapClamped = constrain(sens.ms2.map_kpa, 20.0f, 250.0f);
        int barW = (int)((mapClamped - 20.0f) / 230.0f * 260.0f);

        uint16_t barColor = (sens.ms2.boost_bar > 0.05f) ? COLOR_BMW_M_RED :
                            (sens.ms2.boost_bar > -0.2f) ? COLOR_BMW_AMBER_BRIGHT : COLOR_BMW_M_BLUE;
        tft.fillRect(20, 178, barW, 14, barColor);

        // Метки давления
        tft.fillRect(18, 196, 280, 12, COLOR_BLACK);
        tft.setTextColor(COLOR_MID_GRAY);
        tft.setTextSize(1);
        tft.setCursor(18, 197);
        tft.print("-0.8b");
        tft.setCursor(105, 197);
        tft.print("0.0b (100k)");
        tft.setCursor(195, 197);
        tft.print("+0.8b");
        tft.setCursor(255, 197);
        tft.print("+1.5b");

        // Текущее значение наддува цифрами
        tft.fillRect(180, 158, 125, 14, COLOR_BLACK);
        tft.setTextColor(COLOR_BMW_AMBER_BRIGHT);
        tft.setCursor(185, 160);
        tft.printf("%+.2f Bar (%.0f kPa)", sens.ms2.boost_bar, sens.ms2.map_kpa);
    } else {
        float vClamped = constrain(sens.batteryVoltage, 10.0f, 15.5f);
        int barW = (int)((vClamped - 10.0f) / 5.5f * 260.0f);
        uint16_t barColor = (sens.batteryVoltage < 11.8f) ? COLOR_STATUS_ERR :
                            (sens.batteryVoltage < 13.2f) ? COLOR_BMW_AMBER_MAIN :
                            (sens.batteryVoltage <= 14.7f) ? COLOR_STATUS_OK : COLOR_STATUS_WARN;
        tft.fillRect(20, 178, barW, 14, barColor);

        tft.setTextColor(COLOR_MID_GRAY);
        tft.setTextSize(1);
        tft.setCursor(18, 197);
        tft.print("10V");
        tft.setCursor(110, 197);
        tft.print("12.6V");
        tft.setCursor(195, 197);
        tft.print("14.4V");
        tft.setCursor(265, 197);
        tft.print("15.5V");
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
        
        // Заголовок с ///M триколором
        tft.fillRect(0, 0, SCREEN_WIDTH, 30, COLOR_BLACK);
        BmwAssets::drawMPowerLogo(tft, 12, 6, 18);
        tft.setTextColor(COLOR_WHITE);
        tft.setTextSize(2);
        tft.setCursor(78, 8);
        tft.print("M-PERFORMANCE");

        tft.drawFastHLine(0, 30, SCREEN_WIDTH, COLOR_BMW_M_BLUE);

        // Рамка тахометра / секундомера
        tft.drawRoundRect(10, 38, 300, 130, 5, COLOR_DARK_GRAY);

        drawFooter("КЛИК: СТАРТ/СТОП", sens.ms2Online ? "MS2 LIVE RPM" : "СЕКУНДОМЕР");
    }

    if (sens.ms2Online) {
        // ОТОБРАЖЕНИЕ ОБОРОТОВ ДВИГАТЕЛЯ ИЗ MEGASQUIRT 2
        tft.setTextColor(COLOR_BMW_M_CYAN);
        tft.setTextSize(1);
        tft.setCursor(20, 48);
        tft.print("/// ТАХОМЕТР И НАДДУВ MS2");

        // Большие цифры оборотов
        tft.fillRect(20, 64, 280, 46, COLOR_BLACK);
        uint16_t rpmCol = (sens.ms2.rpm > 6500) ? COLOR_BMW_M_RED :
                          (sens.ms2.rpm > 5500) ? COLOR_BMW_AMBER_BRIGHT : COLOR_WHITE;
        tft.setTextColor(rpmCol);
        tft.setTextSize(4);
        tft.setCursor(25, 70);
        tft.printf("%4d", sens.ms2.rpm);
        tft.setTextSize(2);
        tft.setCursor(145, 84);
        tft.print("RPM");

        // Наддув и УОЗ справа
        tft.setTextSize(1);
        tft.setTextColor(COLOR_BMW_AMBER_MAIN);
        tft.setCursor(200, 72);
        tft.printf("Бар: %+.2f", sens.ms2.boost_bar);
        tft.setCursor(200, 88);
        tft.printf("УОЗ: %.1f", sens.ms2.advance_deg);

        // Динамический Shift-Light / Шкала оборотов (0 - 7500 RPM)
        int rpmW = map(constrain((int)sens.ms2.rpm, 0, 7500), 0, 7500, 0, 276);
        tft.fillRect(22, 122, 276, 16, COLOR_BLACK);
        tft.drawRect(21, 121, 278, 18, COLOR_DARK_GRAY);

        // Трехцветная заливка шкалы в стиле M-Power
        if (rpmW > 0) {
            uint16_t barCol = (sens.ms2.rpm < 4000) ? COLOR_BMW_M_CYAN :
                              (sens.ms2.rpm < 6000) ? COLOR_BMW_M_BLUE : COLOR_BMW_M_RED;
            tft.fillRect(22, 122, rpmW, 16, barCol);
        }

        // Метки шкалы
        tft.fillRect(20, 144, 280, 14, COLOR_BLACK);
        tft.setTextColor(COLOR_MID_GRAY);
        tft.setTextSize(1);
        tft.setCursor(20, 146);
        tft.print("0");
        tft.setCursor(85, 146);
        tft.print("2k");
        tft.setCursor(150, 146);
        tft.print("4k");
        tft.setCursor(215, 146);
        tft.print("6k");
        tft.setTextColor(COLOR_BMW_M_RED);
        tft.setCursor(275, 146);
        tft.print("7.5k");

    } else {
        // Резервный секундомер (если MS2 отключен)
        tft.setTextColor(COLOR_BMW_M_CYAN);
        tft.setTextSize(1);
        tft.setCursor(20, 48);
        tft.print("/// СЕКУНДОМЕР / ЗАМЕР 0-100 (MS2 OFFLINE)");

        unsigned long currentElapsed = stopwatchElapsedMillis;
        if (stopwatchRunning) {
            currentElapsed += (millis() - stopwatchStartMillis);
        }
        unsigned long totalSec = currentElapsed / 1000;
        unsigned long msFraction = (currentElapsed % 1000) / 100;
        int m = totalSec / 60;
        int s = totalSec % 60;

        tft.fillRect(25, 75, 270, 50, COLOR_BLACK);
        tft.setTextColor(stopwatchRunning ? COLOR_BMW_AMBER_BRIGHT : COLOR_WHITE);
        tft.setTextSize(4);
        tft.setCursor(35, 82);
        tft.printf("%02d:%02d.%1d", m, s, (int)msFraction);

        tft.fillRect(28, 138, 250, 18, COLOR_BLACK);
        tft.setTextSize(1);
        tft.setTextColor(stopwatchRunning ? COLOR_STATUS_OK : COLOR_MID_GRAY);
        tft.setCursor(28, 140);
        tft.print(stopwatchRunning ? ">> ЗАМЕР ИДЕТ..." : "[ ГОТОВ К ЗАМЕРУ ]");
    }

    // Декоративная динамическая полоса внизу экрана
    int barLen = (int)((millis() / 50) % 290);
    tft.fillRect(15, 185, 290, 6, COLOR_BLACK);
    tft.fillRect(15, 185, barLen, 6, COLOR_BMW_M_RED);
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
        drawHeader("НАСТРОЙКИ & ДИАГНОСТИКА", false);

        tft.drawRoundRect(10, 32, 300, 175, 4, COLOR_DARK_GRAY);

        tft.setTextColor(COLOR_BMW_AMBER_BRIGHT);
        tft.setTextSize(1);
        tft.setCursor(20, 42);
        tft.print("СТАТУС ШИНЫ MEGASQUIRT 2 (CAN):");

        // Статус связи с MS2
        tft.setCursor(20, 58);
        if (sens.ms2Online) {
            tft.setTextColor(COLOR_STATUS_OK);
            tft.printf("CAN 500k: ПОДКЛЮЧЕН (Пакетов: %lu)", sens.ms2.packetsTotal);
        } else {
            tft.setTextColor(COLOR_STATUS_ERR);
            tft.print("CAN 500k: НЕТ СВЯЗИ (Проверьте CTX/CRX)");
        }

        tft.setTextColor(COLOR_WHITE);
        tft.setCursor(20, 74);
        tft.printf("Wi-Fi Точка:  %s  (Pass: %s)", AP_SSID, AP_PASSWORD);

        tft.setTextColor(COLOR_STATUS_OK);
        tft.setCursor(20, 90);
        tft.print("Web-адрес:    http://192.168.4.1");

        tft.setTextColor(COLOR_MID_GRAY);
        tft.drawFastHLine(20, 106, 280, COLOR_DARK_GRAY);

        tft.setCursor(20, 116);
        tft.printf("Пины CAN:     TX: GPIO %d, RX: GPIO %d", PIN_CAN_TX, PIN_CAN_RX);

        tft.setCursor(20, 132);
        tft.printf("Free Heap:    %d KB / Flash: %d MB", ESP.getFreeHeap() / 1024, ESP.getFlashChipSize() / (1024 * 1024));

        tft.setCursor(20, 148);
        tft.printf("Подсветка:    ШИМ %d / 255", Display.getBrightness());

        tft.setCursor(20, 164);
        tft.printf("NTP Сервер:   %s", NTP_SERVER_1);

        tft.setCursor(20, 180);
        tft.printf("Базовый ID:   1520 (Adv) / 1512 (Dash)");

        drawFooter("КЛИК: ДАЛЕЕ", "BMW E36 MS2");
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
