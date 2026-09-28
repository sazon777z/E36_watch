#include "display_driver.h"

DisplayDriver Display;

DisplayDriver::DisplayDriver()
    : spi(nullptr), tft(nullptr), currentBrightness(0), isInitialized(false) {
}

void DisplayDriver::initBacklightPWM() {
    pinMode(PIN_TFT_BL, OUTPUT);
    digitalWrite(PIN_TFT_BL, HIGH);

#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
    // ESP32 Arduino Core 3.x API
    ledcAttach(PIN_TFT_BL, 5000, 8);
    ledcWrite(PIN_TFT_BL, DEFAULT_BRIGHTNESS);
#else
    // ESP32 Arduino Core 2.x API
    ledcSetup(0, 5000, 8);
    ledcAttachPin(PIN_TFT_BL, 0);
    ledcWrite(0, DEFAULT_BRIGHTNESS);
#endif
    currentBrightness = DEFAULT_BRIGHTNESS;
}

void DisplayDriver::setBrightness(uint8_t brightness) {
    currentBrightness = brightness;
#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
    ledcWrite(PIN_TFT_BL, brightness);
#else
    ledcWrite(0, brightness);
#endif
}

bool DisplayDriver::init() {
    // Аппаратный сброс контроллера ST7789
    pinMode(PIN_TFT_RST, OUTPUT);
    digitalWrite(PIN_TFT_RST, HIGH);
    delay(10);
    digitalWrite(PIN_TFT_RST, LOW);
    delay(20);
    digitalWrite(PIN_TFT_RST, HIGH);
    delay(50);

    initBacklightPWM();

    // Инициализация аппаратного SPI на кастомных пинах ESP32-S3
    spi = new SPIClass(FSPI);
    spi->begin(PIN_TFT_SCL, -1, PIN_TFT_SDA, PIN_TFT_CS);

    // Создание инстанса драйвера ST7789
    tft = new Adafruit_ST7789(spi, PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST);

    // Стандартная инициализация ST7789 для дисплея 240x320
    tft->init(240, 320);
    tft->setSPISpeed(40000000); // 40 MHz SPI для мгновенной отрисовки без задержек
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
