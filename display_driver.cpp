#include "display_driver.h"

DisplayDriver Display;

DisplayDriver::DisplayDriver()
    : spi(nullptr), tft(nullptr), currentBrightness(0), isInitialized(false) {
}

void DisplayDriver::initBacklight() {
    pinMode(PIN_TFT_BL, OUTPUT);
    digitalWrite(PIN_TFT_BL, HIGH); // Постоянная 100% максимальная яркость подсветки
    currentBrightness = 255;
}

void DisplayDriver::setBrightness(uint8_t brightness) {
    (void)brightness;
    // Подсветка всегда включена на 100%
    pinMode(PIN_TFT_BL, OUTPUT);
    digitalWrite(PIN_TFT_BL, HIGH);
    currentBrightness = 255;
}

bool DisplayDriver::init() {
    // 1. Принудительно выключаем подсветку при старте (гарантирует отсутствие белого шума и вспышек)
    pinMode(PIN_TFT_BL, OUTPUT);
    digitalWrite(PIN_TFT_BL, LOW);
    currentBrightness = 0;

    // 2. Деактивируем Chip Select (CS = HIGH) и переводим DC в HIGH.
    // Линию тактирования SPI (SCL) не переводим в HIGH, так как ST7789 работает в SPI Mode 0 (CPOL=0).
    pinMode(PIN_TFT_CS, OUTPUT);
    digitalWrite(PIN_TFT_CS, HIGH);
    pinMode(PIN_TFT_DC, OUTPUT);
    digitalWrite(PIN_TFT_DC, HIGH);

    // 3. Надежный аппаратный импульс сброса контроллера ST7789
    pinMode(PIN_TFT_RST, OUTPUT);
    digitalWrite(PIN_TFT_RST, HIGH);
    delay(20);
    digitalWrite(PIN_TFT_RST, LOW);
    delay(50);
    digitalWrite(PIN_TFT_RST, HIGH);
    delay(150);

    // 4. Инициализация аппаратного SPI на кастомных пинах ESP32-S3
    // Передаем SS = -1, так как библиотека Adafruit_SPITFT управляет пином CS (GPIO 9) вручную.
    // Это исключает захват пина аппаратным модулем SPI и устраняет черный экран при аппаратном сбросе.
    if (!spi) {
        spi = new SPIClass(FSPI);
        spi->begin(PIN_TFT_SCL, -1, PIN_TFT_SDA, -1);
    }

    if (!tft) {
        // Передаем RST = -1, так как аппаратный сброс мы уже выполнили выше с точными таймингами
        tft = new Adafruit_ST7789(spi, PIN_TFT_CS, PIN_TFT_DC, -1);
    }

    // 5. Стандартная инициализация ST7789 для дисплея 240x320
    tft->init(240, 320);
    tft->setSPISpeed(20000000); // 20 MHz SPI - стабильная скорость без помех
    tft->setRotation(DISPLAY_ROTATION);

    // Пауза 120 мс для встроенных генераторов напряжений (charge pumps) Sitronix ST7789.
    // Библиотека tft->init() шлет SLPOUT (0x11) с задержкой всего 10 мс, тогда как согласно
    // спецификации Sitronix, аналоговым накачкам требуется 120 мс до активации развертки (DISPON).
    delay(120);
    tft->sendCommand(ST77XX_NORON);
    delay(10);
    tft->sendCommand(ST77XX_DISPON);
    delay(50);

    // 6. Первичная заливка черным цветом до включения подсветки матрицы
    tft->fillScreen(COLOR_BLACK);

    isInitialized = true;

    // 7. Включаем подсветку только ПОСЛЕ того, как матрица полностью проинициализирована и заполнена черным
    initBacklight();
    return true;
}

void DisplayDriver::fadeIn(uint8_t targetBrightness, uint16_t delayMs) {
    (void)targetBrightness;
    (void)delayMs;
    setBrightness(255);
}

void DisplayDriver::fadeOut(uint16_t delayMs) {
    (void)delayMs;
}

void DisplayDriver::clear(uint16_t color) {
    if (tft) {
        tft->fillScreen(color);
    }
}
