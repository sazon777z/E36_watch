#include "display_driver.h"

DisplayDriver Display;

DisplayDriver::DisplayDriver()
    : spi(nullptr), tft(nullptr), currentBrightness(0), isInitialized(false) {
}

void DisplayDriver::initBacklightPWM() {
    pinMode(PIN_TFT_BL, OUTPUT);
    digitalWrite(PIN_TFT_BL, HIGH); // Прямое гарантированное включение подсветки
    currentBrightness = 255;
}

void DisplayDriver::setBrightness(uint8_t brightness) {
    currentBrightness = brightness;
    pinMode(PIN_TFT_BL, OUTPUT);
    if (brightness > 0) {
        digitalWrite(PIN_TFT_BL, HIGH);
    } else {
        digitalWrite(PIN_TFT_BL, LOW);
    }
}

bool DisplayDriver::init() {
    // Надежный аппаратный сброс контроллера ST7789
    pinMode(PIN_TFT_RST, OUTPUT);
    digitalWrite(PIN_TFT_RST, HIGH);
    delay(20);
    digitalWrite(PIN_TFT_RST, LOW);
    delay(50);
    digitalWrite(PIN_TFT_RST, HIGH);
    delay(150);

    initBacklightPWM();

    // Инициализация аппаратного SPI на кастомных пинах ESP32-S3
    spi = new SPIClass(FSPI);
    spi->begin(PIN_TFT_SCL, -1, PIN_TFT_SDA, PIN_TFT_CS);

    // Создание инстанса драйвера ST7789
    tft = new Adafruit_ST7789(spi, PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST);

    // Стандартная инициализация ST7789 для дисплея 240x320
    tft->init(240, 320);
    tft->setSPISpeed(27000000); // 27 MHz SPI - гарантированная стабильность и отсутствие помех
    tft->setRotation(DISPLAY_ROTATION);
    tft->fillScreen(COLOR_BLACK);

    isInitialized = true;
    setBrightness(DEFAULT_BRIGHTNESS);
    return true;
}

void DisplayDriver::fadeIn(uint8_t targetBrightness, uint16_t delayMs) {
    int start = currentBrightness;
    for (int b = start; b <= targetBrightness; b += 5) {
        setBrightness((uint8_t)(b > 255 ? 255 : b));
        delay(delayMs);
    }
    setBrightness(targetBrightness);
}

void DisplayDriver::fadeOut(uint16_t delayMs) {
    int start = currentBrightness;
    for (int b = start; b >= 0; b -= 5) {
        setBrightness((uint8_t)(b < 0 ? 0 : b));
        delay(delayMs);
    }
    setBrightness(0);
}

void DisplayDriver::clear(uint16_t color) {
    if (tft) {
        tft->fillScreen(color);
    }
}
