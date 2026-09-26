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
#define PIN_VOLTAGE_ADC   7   // АЦП для измерения напряжения АКБ (через делитель)
#define PIN_BTN_NEXT      14  // Кнопка переключения экранов (с подтяжкой к VCC/GND)
#define PIN_ONEWIRE_TEMP  15  // Шина 1-Wire для датчика температуры DS18B20
#define PIN_ILLUMINATION  16  // Вход сигнала габаритов/фар (для авто-диммирования)

// =============================================================================
// НАСТРОЙКИ CAN-ШИНЫ (WCMCU-230 / SN65HVD230) ДЛЯ MEGASQUIRT 2
// =============================================================================
#define PIN_CAN_TX        43  // CTX трансивера (вывод TX платы ESP32-S3)
#define PIN_CAN_RX        44  // CRX трансивера (вывод RX платы ESP32-S3)
#define CAN_SPEED_KBPS    500 // Стандартная скорость шины CAN MegaSquirt 2
#define MS2_BASE_ID_ADV   1520 // Расширенное вещание MS2 Realtime Broadcast
#define MS2_BASE_ID_DASH  1512 // Упрощенное вещание MS2 Dash Broadcast
#define MS2_TEMP_IS_FAHR  true // Флаг: если в MS2 температура транслируется в 0.1 deg F

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
// НАСТРОЙКИ ВРЕМЕНИ И WI-FI
// =============================================================================
#define DEFAULT_TIMEZONE_OFFSET 3     // Смещение часового пояса (UTC+3, например, Москва)
#define DEFAULT_TIMEZONE_MIN    0
#define NTP_SERVER_1            "pool.ntp.org"
#define NTP_SERVER_2            "time.nist.gov"
#define NTP_SYNC_INTERVAL_SEC   3600  // Синхронизация раз в час

// Wi-Fi точка доступа для настроек
#define AP_SSID                 "BMW_E36_OBC"
#define AP_PASSWORD             "12345678"
#define AP_IP_OCTET             4     // 192.168.4.1

// Имя хоста mDNS (http://bmw-e36.local)
#define MDNS_HOSTNAME           "bmw-e36"

// =============================================================================
// НАСТРОЙКИ ВОЛЬТМЕТРА И БОРТСЕТИ
// =============================================================================
// Коэффициент делителя напряжения: (R1 + R2) / R2
// Например, R1 = 30k, R2 = 7.5k -> (30 + 7.5) / 7.5 = 5.0
#define VOLTAGE_DIVIDER_RATIO   5.15f 
#define ADC_VREF                3.30f // Опорное напряжение АЦП ESP32-S3
#define ADC_RESOLUTION          4095.0f

#endif // CONFIG_H
