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
    // Надежный аппаратный сброс контроллера ST7789
    pinMode(PIN_TFT_RST, OUTPUT);
    digitalWrite(PIN_TFT_RST, HIGH);
    delay(50);
    digitalWrite(PIN_TFT_RST, LOW);
    delay(50);
    digitalWrite(PIN_TFT_RST, HIGH);
    delay(150);

    initBacklight();

    // Инициализация аппаратного SPI на кастомных пинах ESP32-S3
    spi = new SPIClass(FSPI);
    spi->begin(PIN_TFT_SCL, -1, PIN_TFT_SDA, PIN_TFT_CS);

    // Создание инстанса драйвера ST7789
    tft = new Adafruit_ST7789(spi, PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST);

    // Стандартная инициализация ST7789 для дисплея 240x320
    tft->init(240, 320);
    tft->setSPISpeed(20000000); // 20 MHz SPI - гарантированная стабильность фронтов без звона и срывов
    tft->setRotation(DISPLAY_ROTATION);
    tft->fillScreen(COLOR_BLACK);

    isInitialized = true;
    setBrightness(255);
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
