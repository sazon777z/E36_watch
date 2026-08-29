#ifndef DISPLAY_DRIVER_H
#define DISPLAY_DRIVER_H

#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include "config.h"
#include "bmw_theme.h"

class DisplayDriver {
public:
    DisplayDriver();

    // Инициализация SPI, экрана ST7789 и ШИМ подсветки
    bool init();

    // Управление подсветкой
    void setBrightness(uint8_t brightness);
    uint8_t getBrightness() const { return currentBrightness; }
    void fadeIn(uint8_t targetBrightness = DEFAULT_BRIGHTNESS, uint16_t delayMs = FADE_STEP_DELAY_MS);
    void fadeOut(uint16_t delayMs = FADE_STEP_DELAY_MS);

    // Доступ к объекту Adafruit_ST7789
    Adafruit_ST7789& getTft() { return *tft; }

    // Очистка экрана
    void clear(uint16_t color = COLOR_BLACK);

private:
    SPIClass* spi;
    Adafruit_ST7789* tft;
    uint8_t currentBrightness;
    bool isInitialized;

    void initBacklightPWM();
};

extern DisplayDriver Display;

#endif // DISPLAY_DRIVER_H
