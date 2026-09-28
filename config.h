#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// =============================================================================
// НАЗНАЧЕНИЕ ПИНОВ (PINOUT) ESP32-S3
// =============================================================================
// Дисплей GMT024-08 (ST7789 SPI TFT 240x320)
#define PIN_TFT_SCL       13  // SPI Clock (SCK)
#define PIN_TFT_SDA       12  // SPI Data / MOSI
#define PIN_TFT_RST       11  // Reset
#define PIN_TFT_DC        10  // Data / Command
#define PIN_TFT_CS        9   // Chip Select
#define PIN_TFT_BL        8   // Подсветка (ШИМ / LEDC)

// Дополнительные входы/выходы (при необходимости)
#define PIN_VOLTAGE_ADC   -1  // Отключен (напряжение читается по CAN из MS2; GPIO 7 занят под CAN_RX)
#define PIN_BTN_NEXT      14  // Внешняя кнопка переключения экранов (GPIO 14 к GND)
#define PIN_BTN_BOOT      0   // Встроенная кнопка BOOT на плате ESP32-S3 (GPIO 0 к GND)
#define PIN_ONEWIRE_TEMP  15  // Шина 1-Wire для датчика температуры DS18B20
#define PIN_ILLUMINATION  16  // Вход сигнала габаритов/фар (для авто-диммирования)

// =============================================================================
// НАСТРОЙКИ CAN-ШИНЫ (WCMCU-230 / SN65HVD230) ДЛЯ MEGASQUIRT 2
// =============================================================================
#define PIN_CAN_TX        6   // CTX трансивера WCMCU-230 (GPIO 6)
#define PIN_CAN_RX        7   // CRX трансивера WCMCU-230 (GPIO 7)
#define CAN_SPEED_KBPS    500 // Стандартная скорость шины CAN MegaSquirt 2
#define MS2_BASE_ID_ADV   1520 // Расширенное вещание MS2 Realtime Broadcast
#define MS2_BASE_ID_DASH  1512 // Упрощенное вещание MS2 Dash Broadcast
#define MS2_TEMP_IS_FAHR  true // Флаг: если в MS2 температура транслируется в 0.1 deg F

// =============================================================================
// НАСТРОЙКИ GPS МОДУЛЯ (u-blox NEO-7M)
// =============================================================================
#define PIN_GPS_RX        44  // RX ESP32-S3 <- TX модуля GPS NEO-7M
#define PIN_GPS_TX        43  // TX ESP32-S3 -> RX модуля GPS NEO-7M
#define GPS_BAUDRATE      9600

// =============================================================================
// КАЛИБРОВКИ РАСХОДА ТОПЛИВА (ДЛЯ MEGASQUIRT 2)
// =============================================================================
#define ENGINE_CYLINDERS          6      // Число цилиндров (6 для M50/M52/S50, 4 для M40/M42)
#define INJECTOR_FLOW_CC_MIN      200.0f // Производительность форсунок (сс/мин при 3.0 бар)
#define INJECTOR_DEAD_TIME_US     900    // Лаг/мертвое время форсунок (мкс, обычно 0.9 мс)
#define FUEL_CALIBRATION_FACTOR   1.00f  // Поправочный коэффициент для точной калибровки

// =============================================================================
// НАСТРОЙКИ ДИСПЛЕЯ
// =============================================================================
#define SCREEN_WIDTH      320
#define SCREEN_HEIGHT     240
// 1 = Альбомная (320x240), 3 = Альбомная перевернутая, 0 = Портретная (240x320)
#define DISPLAY_ROTATION  1   

#define DEFAULT_BRIGHTNESS      220   // Яркость по умолчанию (0-255)
#define NIGHT_BRIGHTNESS        50    // Яркость при включенных габаритах (0-255)
#define FADE_STEP_DELAY_MS      4     // Скорость плавного розжига/затухания

// =============================================================================
// НАСТРОЙКИ BLUETOOTH LOW ENERGY (BLE)
// =============================================================================
#define BLE_DEVICE_NAME         "BMW-E36-OBC"
// Стандартный Nordic UART Service (NUS)
#define BLE_SERVICE_UUID        "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define BLE_CHAR_RX_UUID        "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define BLE_CHAR_TX_UUID        "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"
#define DEFAULT_TIMEZONE_OFFSET 3     // Смещение часового пояса (UTC+3)

// =============================================================================
// НАСТРОЙКИ ВОЛЬТМЕТРА И БОРТСЕТИ
// =============================================================================
// Коэффициент делителя напряжения: (R1 + R2) / R2
// Например, R1 = 30k, R2 = 7.5k -> (30 + 7.5) / 7.5 = 5.0
#define VOLTAGE_DIVIDER_RATIO   5.15f 
#define ADC_VREF                3.30f // Опорное напряжение АЦП ESP32-S3
#define ADC_RESOLUTION          4095.0f

#endif // CONFIG_H
