#ifndef BMW_ASSETS_H
#define BMW_ASSETS_H

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include "bmw_theme.h"

// =============================================================================
// ВЕКТОРНАЯ ГРАФИКА И ИКОНКИ BMW E36
// =============================================================================

class BmwAssets {
public:
    // Отрисовка фирменного логотипа BMW (круглый пропеллер)
    static void drawRoundel(Adafruit_ST7789& tft, int16_t cx, int16_t cy, int16_t r) {
        if (r < 6) return;

        // Внешнее черное кольцо с хромированной/серебристой каймой
        tft.fillCircle(cx, cy, r, COLOR_WHITE);
        tft.fillCircle(cx, cy, r - 2, COLOR_BLACK);

        int16_t innerR = (r * 7) / 10;
        int16_t innerR2 = innerR - 1;

        // Внутренняя черная разделительная окружность
        tft.drawCircle(cx, cy, innerR + 1, COLOR_WHITE);

        // 4 сектора (2 синих, 2 белых)
        // Верхний левый - синий, верхний правый - белый
        // Нижний левый - белый, нижний правый - синий
        for (int16_t dy = -innerR2; dy <= innerR2; dy++) {
            for (int16_t dx = -innerR2; dx <= innerR2; dx++) {
                if (dx * dx + dy * dy <= innerR2 * innerR2) {
                    uint16_t color;
                    if ((dx < 0 && dy < 0) || (dx >= 0 && dy >= 0)) {
                        color = COLOR_BMW_M_BLUE; // Синий сектор
                    } else {
                        color = COLOR_WHITE;      // Белый сектор
                    }
                    tft.drawPixel(cx + dx, cy + dy, color);
                }
            }
        }

        // Тонкий перекрестный разделитель между секторами
        tft.drawFastHLine(cx - innerR2, cy, innerR2 * 2 + 1, COLOR_BLACK);
        tft.drawFastVLine(cx, cy - innerR2, innerR2 * 2 + 1, COLOR_BLACK);
    }

    // Отрисовка триколора BMW M-Power с литерой "///M"
    static void drawMPowerLogo(Adafruit_ST7789& tft, int16_t x, int16_t y, int16_t h) {
        int16_t stripeW = (h * 4) / 10;
        int16_t slant = h / 3;

        // Три наклонные полосы: Cyan, Dark Blue, Red
        auto drawSlantedStripe = [&](int16_t sx, uint16_t color) {
            for (int16_t i = 0; i < h; i++) {
                int16_t curX = sx + (i * slant) / h;
                tft.drawFastHLine(curX, y + (h - 1 - i), stripeW, color);
            }
        };

        drawSlantedStripe(x, COLOR_BMW_M_CYAN);
        drawSlantedStripe(x + stripeW + 1, COLOR_BMW_M_BLUE);
        drawSlantedStripe(x + (stripeW + 1) * 2, COLOR_BMW_M_RED);

        // Буква 'M'
        int16_t mX = x + (stripeW + 1) * 3 + slant + 4;
        tft.setTextColor(COLOR_WHITE);
        tft.setTextSize(h > 24 ? 3 : (h > 14 ? 2 : 1));
        tft.setCursor(mX, y + (h / 6));
        tft.print("M");
    }

    // Иконка аккумулятора (для вольтметра)
    static void drawBattery(Adafruit_ST7789& tft, int16_t x, int16_t y, uint16_t color, float voltage) {
        // Корпус АКБ
        tft.drawRect(x, y, 22, 12, color);
        tft.fillRect(x + 22, y + 3, 2, 6, color); // Плюсовой вывод

        // Знаки + и -
        tft.drawFastHLine(x + 3, y + 6, 4, color);
        tft.drawFastVLine(x + 5, y + 4, 5, color); // '+'

        tft.drawFastHLine(x + 13, y + 6, 4, color); // '-'

        // Индикатор уровня (если напряжение < 12V - подсветка красным)
        uint16_t barColor = (voltage < 11.8f) ? COLOR_STATUS_ERR : 
                            (voltage > 14.8f) ? COLOR_STATUS_WARN : color;
        
        int16_t fillW = 0;
        if (voltage >= 11.5f) fillW = 4;
        if (voltage >= 12.2f) fillW = 8;
        if (voltage >= 12.6f) fillW = 12;
        if (voltage >= 13.5f) fillW = 14;

        if (fillW > 0) {
            tft.fillRect(x + 2, y + 2, fillW, 8, barColor);
        }
    }

    // Иконка Bluetooth
    static void drawBluetooth(Adafruit_ST7789& tft, int16_t x, int16_t y, uint16_t color, bool connected) {
        uint16_t c = connected ? color : COLOR_MID_GRAY;
        // Центральная вертикальная линия
        tft.drawFastVLine(x + 5, y + 1, 13, c);
        tft.drawFastVLine(x + 6, y + 1, 13, c);
        // Верхний и нижний шевроны символа Bluetooth
        tft.drawLine(x + 2, y + 4, x + 9, y + 10, c);
        tft.drawLine(x + 9, y + 10, x + 5, y + 14, c);
        tft.drawLine(x + 2, y + 10, x + 9, y + 4, c);
        tft.drawLine(x + 9, y + 4, x + 5, y, c);

        if (!connected) {
            // Маленькая точка снизу при ожидании подключения
            tft.drawPixel(x + 5, y + 16, COLOR_DARK_GRAY);
        }
    }

    // Иконка Wi-Fi
    static void drawWifi(Adafruit_ST7789& tft, int16_t x, int16_t y, uint16_t color, bool connected) {
        if (!connected) {
            // Перечеркнутый значок или точка
            tft.fillCircle(x + 8, y + 10, 2, COLOR_MID_GRAY);
            tft.drawLine(x + 2, y + 2, x + 14, y + 12, COLOR_BMW_AMBER_DIM);
            return;
        }
        // Точка
        tft.fillCircle(x + 8, y + 10, 2, color);
        // Дуга 1
        tft.drawCircleHelper(x + 8, y + 10, 5, 1 | 2, color);
        // Дуга 2
        tft.drawCircleHelper(x + 8, y + 10, 9, 1 | 2, color);
    }

    // Иконка термометра
    static void drawThermometer(Adafruit_ST7789& tft, int16_t x, int16_t y, uint16_t color) {
        tft.drawRect(x + 3, y, 4, 10, color);
        tft.fillCircle(x + 5, y + 11, 4, color);
        tft.fillRect(x + 4, y + 5, 2, 6, color);
    }

    // Иконка спутника GPS
    static void drawSatellite(Adafruit_ST7789& tft, int16_t x, int16_t y, uint16_t color, bool hasFix) {
        uint16_t c = hasFix ? color : COLOR_MID_GRAY;
        // Корпус спутника
        tft.drawCircle(x + 6, y + 6, 3, c);
        if (hasFix) {
            tft.fillCircle(x + 6, y + 6, 2, c);
        }
        // Солнечные панели (крылья)
        tft.drawFastHLine(x + 1, y + 6, 2, c);
        tft.drawFastHLine(x + 10, y + 6, 2, c);
        tft.drawFastVLine(x + 1, y + 4, 5, c);
        tft.drawFastVLine(x + 11, y + 4, 5, c);
        // Дуга радиосигнала
        if (hasFix) {
            tft.drawCircleHelper(x + 6, y + 6, 6, 4 | 8, c);
        }
    }

    // Аутентичный 7-сегментный индикатор BMW OBC (с теневыми неактивными сегментами)
    // Поддерживает цифры '0'-'9', ':', '-', ' '
    static void draw7SegmentDigit(Adafruit_ST7789& tft, int16_t x, int16_t y, char ch, 
                                  uint16_t activeColor, uint16_t ghostColor, uint8_t size = 4) {
        // Размеры сегментов, пропорциональные size
        int16_t sw = size * 5;      // Ширина горизонтального сегмента
        int16_t sh = size;          // Толщина сегмента
        int16_t vl = size * 6;      // Длина вертикального сегмента

        // Маска сегментов: 0bGFEDCBA
        // A: верх, B: верх-право, C: низ-право, D: низ, E: низ-лево, F: верх-лево, G: середина
        uint8_t mask = 0;
        switch (ch) {
            case '0': mask = 0b00111111; break;
            case '1': mask = 0b00000110; break;
            case '2': mask = 0b01011011; break;
            case '3': mask = 0b01001111; break;
            case '4': mask = 0b01100110; break;
            case '5': mask = 0b01101101; break;
            case '6': mask = 0b01111101; break;
            case '7': mask = 0b00000111; break;
            case '8': mask = 0b01111111; break;
            case '9': mask = 0b01101111; break;
            case '-': mask = 0b01000000; break;
            case ' ': mask = 0b00000000; break;
            case 'C': mask = 0b00111001; break;
            case 'E': mask = 0b01111001; break;
            case 'F': mask = 0b01110001; break;
            case 'P': mask = 0b01110011; break;
            case 'L': mask = 0b00111000; break;
            case 'U': mask = 0b00111110; break;
            default:  mask = 0; break;
        }

        // Сегмент A (верхний горизонтальный)
        tft.fillRect(x + sh, y, sw, sh, (mask & 0x01) ? activeColor : ghostColor);

        // Сегмент B (верхний правый)
        tft.fillRect(x + sh + sw, y + sh, sh, vl, (mask & 0x02) ? activeColor : ghostColor);

        // Сегмент C (нижний правый)
        tft.fillRect(x + sh + sw, y + sh * 2 + vl, sh, vl, (mask & 0x04) ? activeColor : ghostColor);

        // Сегмент D (нижний горизонтальный)
        tft.fillRect(x + sh, y + (sh + vl) * 2, sw, sh, (mask & 0x08) ? activeColor : ghostColor);

        // Сегмент E (нижний левый)
        tft.fillRect(x, y + sh * 2 + vl, sh, vl, (mask & 0x10) ? activeColor : ghostColor);

        // Сегмент F (верхний левый)
        tft.fillRect(x, y + sh, sh, vl, (mask & 0x20) ? activeColor : ghostColor);

        // Сегмент G (средний горизонтальный)
        tft.fillRect(x + sh, y + sh + vl, sw, sh, (mask & 0x40) ? activeColor : ghostColor);
    }

    // Двоеточие для часов в стиле 7-сегментного индикатора E36
    static void draw7SegmentColon(Adafruit_ST7789& tft, int16_t x, int16_t y, bool visible, 
                                  uint16_t activeColor, uint16_t ghostColor, uint8_t size = 4) {
        int16_t dotSize = size * 2;
        int16_t sh = size;
        int16_t vl = size * 6;

        uint16_t col = visible ? activeColor : ghostColor;
        tft.fillRect(x, y + sh + (vl / 2) - (dotSize / 2), dotSize, dotSize, col);
        tft.fillRect(x, y + sh * 2 + vl + (vl / 2) - (dotSize / 2), dotSize, dotSize, col);
    }

    // =========================================================================
    // ЛАКОНИЧНЫЙ ЖИРНЫЙ НАКЛОННЫЙ ЗНАКОГЕНЕРАТОР (BOLD SLANTED / ITALIC)
    // =========================================================================
    static void drawSlantedBoldDigit(Adafruit_ST7789& tft, int16_t x, int16_t y, char ch,
                                     uint16_t activeColor, uint16_t ghostColor,
                                     uint8_t size = 4, int8_t slant = 12) {
        // Размеры сегментов (жирные пропорции)
        int16_t sw = size * 5;      // Длина горизонтального сегмента
        int16_t sh = size + 1;      // Увеличенная толщина для жирности (Bold)
        int16_t vl = size * 6;      // Длина вертикального сегмента
        int16_t totalH = sh * 3 + vl * 2; // Полная высота знакоместа

        // Маска сегментов: 0bGFEDCBA
        uint8_t mask = 0;
        switch (ch) {
            case '0': mask = 0b00111111; break;
            case '1': mask = 0b00000110; break;
            case '2': mask = 0b01011011; break;
            case '3': mask = 0b01001111; break;
            case '4': mask = 0b01100110; break;
            case '5': mask = 0b01101101; break;
            case '6': mask = 0b01111101; break;
            case '7': mask = 0b00000111; break;
            case '8': mask = 0b01111111; break;
            case '9': mask = 0b01101111; break;
            case '-': mask = 0b01000000; break;
            case ' ': mask = 0b00000000; break;
            case 'C': mask = 0b00111001; break;
            case 'E': mask = 0b01111001; break;
            case 'F': mask = 0b01110001; break;
            case 'P': mask = 0b01110011; break;
            case 'L': mask = 0b00111000; break;
            case 'U': mask = 0b00111110; break;
            case 'b': mask = 0b01111100; break;
            case 'd': mask = 0b01011110; break;
            default:  mask = 0; break;
        }

        // Лямбда быстрой построчной отрисовки сегмента с динамическим наклоном
        auto drawSlantedBlock = [&](int16_t x_rel, int16_t y_rel, int16_t w, int16_t h, uint16_t col) {
            for (int16_t dy = 0; dy < h; dy++) {
                int16_t curY = y + y_rel + dy;
                // Наклон вправо вверху (курсив):
                int16_t dx = (int16_t)(((totalH - 1 - (y_rel + dy)) * slant) / totalH);
                tft.drawFastHLine(x + x_rel + dx, curY, w, col);
            }
        };

        // Сегмент A (верх)
        drawSlantedBlock(sh, 0, sw, sh, (mask & 0x01) ? activeColor : ghostColor);

        // Сегмент B (верх-право)
        drawSlantedBlock(sh + sw, sh, sh, vl, (mask & 0x02) ? activeColor : ghostColor);

        // Сегмент C (низ-право)
        drawSlantedBlock(sh + sw, sh * 2 + vl, sh, vl, (mask & 0x04) ? activeColor : ghostColor);

        // Сегмент D (низ)
        drawSlantedBlock(sh, (sh + vl) * 2, sw, sh, (mask & 0x08) ? activeColor : ghostColor);

        // Сегмент E (низ-лево)
        drawSlantedBlock(0, sh * 2 + vl, sh, vl, (mask & 0x10) ? activeColor : ghostColor);

        // Сегмент F (верх-лево)
        drawSlantedBlock(0, sh, sh, vl, (mask & 0x20) ? activeColor : ghostColor);

        // Сегмент G (середина)
        drawSlantedBlock(sh, sh + vl, sw, sh, (mask & 0x40) ? activeColor : ghostColor);
    }

    // Наклонное жирное двоеточие
    static void drawSlantedColon(Adafruit_ST7789& tft, int16_t x, int16_t y, bool visible,
                                 uint16_t activeColor, uint16_t ghostColor,
                                 uint8_t size = 4, int8_t slant = 12) {
        int16_t dotSize = size * 2;
        int16_t sh = size + 1;
        int16_t vl = size * 6;
        int16_t totalH = sh * 3 + vl * 2;

        uint16_t col = visible ? activeColor : ghostColor;

        auto drawSlantedDot = [&](int16_t y_rel) {
            for (int16_t dy = 0; dy < dotSize; dy++) {
                int16_t curY = y + y_rel + dy;
                int16_t dx = (int16_t)(((totalH - 1 - (y_rel + dy)) * slant) / totalH);
                tft.drawFastHLine(x + dx, curY, dotSize, col);
            }
        };

        drawSlantedDot(sh + (vl / 2) - (dotSize / 2));
        drawSlantedDot(sh * 2 + vl + (vl / 2) - (dotSize / 2));
    }
};

#endif // BMW_ASSETS_H
