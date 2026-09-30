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
    initBacklight();

    // 1. Предварительно переводим все управляющие выводы SPI в пассивный уровень HIGH
    // Это исключает ложные срабатывания интерфейса дисплея при сбросе платы кнопкой EN/RESET
    pinMode(PIN_TFT_CS, OUTPUT);
    digitalWrite(PIN_TFT_CS, HIGH); // CS деактивирован
    pinMode(PIN_TFT_DC, OUTPUT);
    digitalWrite(PIN_TFT_DC, HIGH);
    pinMode(PIN_TFT_SCL, OUTPUT);
    digitalWrite(PIN_TFT_SCL, HIGH);
    pinMode(PIN_TFT_SDA, OUTPUT);
    digitalWrite(PIN_TFT_SDA, HIGH);

    // 2. Аппаратный импульс сброса контроллера ST7789
    pinMode(PIN_TFT_RST, OUTPUT);
    digitalWrite(PIN_TFT_RST, HIGH);
    delay(20);
    digitalWrite(PIN_TFT_RST, LOW);
    delay(50);
    digitalWrite(PIN_TFT_RST, HIGH);
    delay(150);

    // 3. Инициализация аппаратного SPI на кастомных пинах ESP32-S3
    if (!spi) {
        spi = new SPIClass(FSPI);
        spi->begin(PIN_TFT_SCL, -1, PIN_TFT_SDA, PIN_TFT_CS);
    }

    if (!tft) {
        tft = new Adafruit_ST7789(spi, PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST);
    }

    // 4. Инициализация ST7789 для дисплея 240x320
    tft->init(240, 320);
    tft->setSPISpeed(20000000); // 20 MHz SPI - стабильная скорость без помех
    tft->setRotation(DISPLAY_ROTATION);

    // 5. Пауза 120 мс для встроенных генераторов напряжений (charge pumps) Sitronix ST7789
    // Библиотека tft->init() уже отправляет SLPOUT (0x11), поэтому повторно слать SLPOUT не нужно,
    // но критически важно выдержать паузу 120 мс перед включением развертки матрицы
    delay(120);
    tft->sendCommand(ST77XX_NORON);  // Обычный режим отображения
    tft->sendCommand(ST77XX_DISPON); // Включение вывода изображения
    delay(50);

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
