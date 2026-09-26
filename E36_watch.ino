/*
 * ============================================================================
 * ПРОЕКТ: БОРТОВОЙ КОМПЬЮТЕР И ЧАСЫ ДЛЯ BMW E36 (1991)
 * ПЛАТФОРМА: ESP32-S3
 * ДИСПЛЕЙ: GMT024-08 (ST7789 240x320 SPI TFT)
 * 
 * РАСПИНОВКА (PINOUT):
 *   SCL (SCK)  -> GPIO 13
 *   SDA (MOSI) -> GPIO 12
 *   RST        -> GPIO 11
 *   DC         -> GPIO 10
 *   CS         -> GPIO 9
 *   BL         -> GPIO 8 (ШИМ регулировка яркости)
 * 
 * CAN-ШИНА MEGASQUIRT 2 (WCMCU-230 / SN65HVD230):
 *   CTX (CAN TX) -> GPIO 43 (вывод TX платы ESP32-S3)
 *   CRX (CAN RX) -> GPIO 44 (вывод RX платы ESP32-S3)
 *   3V3          -> 3.3V (питание)
 *   GND          -> GND (земля)
 * 
 * ОПЦИОНАЛЬНЫЕ ВХОДЫ:
 *   ADC АКБ 12V -> GPIO 7 (через делитель напряжения)
 *   КНОПКА      -> GPIO 14 (кнопка переключения экранов)
 *   ГАБАРИТЫ    -> GPIO 16 (сигнал фар для ночного режима)
 * ============================================================================
 */

#include <Arduino.h>
#include "config.h"
#include "bmw_theme.h"
#include "display_driver.h"
#include "can_driver.h"
#include "time_keeper.h"
#include "sensors.h"
#include "ui_engine.h"
#include "web_portal.h"

// Тайминги выполнения задач
unsigned long lastSensorUpdate = 0;
unsigned long lastUiUpdate = 0;
unsigned long lastNtpCheck = 0;

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("\n[BMW E36 OBC] Инициализация системы...");

    // 1. Инициализация дисплея ST7789 и аппаратного SPI
    if (!Display.init()) {
        Serial.println("[ERROR] Ошибка инициализации дисплея ST7789!");
    } else {
        Serial.println("[OK] Дисплей ST7789 успешно инициализирован.");
    }

    // 2. Инициализация аппаратного контроллера CAN (TWAI) для MegaSquirt 2
    if (!CanBus.init()) {
        Serial.println("[WARN] CAN-шина не запущена, проверьте подключение WCMCU-230.");
    } else {
        Serial.println("[OK] CAN-шина MegaSquirt 2 (500 kbps) запущена.");
    }

    // 3. Отображение фирменной заставки BMW
    UI.showBootSplash("ON-BOARD COMPUTER");

    // 4. Инициализация часов, сенсоров и веб-портала
    Time.init();
    Sensors.init();
    UI.init();
    Portal.init();

    Serial.println("[OK] Точка доступа Wi-Fi запущена: " AP_SSID);
    Serial.println("[OK] Веб-интерфейс доступен по адресу: http://192.168.4.1");
}

void loop() {
    // 1. Постоянный неблокирующий прием пакетов CAN-шины MegaSquirt 2
    CanBus.update();

    unsigned long currentMillis = millis();

    // 2. Обслуживание веб-сервера настроек со смартфона
    Portal.update();

    // 3. Периодический опрос датчиков и АЦП бортсети (каждые 100 мс)
    if (currentMillis - lastSensorUpdate >= 100) {
        lastSensorUpdate = currentMillis;
        Sensors.update();

        // Автоматическое диммирование при включении габаритов
        if (Sensors.getData().headlightsOn) {
            if (Display.getBrightness() > NIGHT_BRIGHTNESS) {
                Display.setBrightness(NIGHT_BRIGHTNESS);
            }
        }
    }

    // 3. Обработка нажатий физической кнопки
    if (Sensors.isNextButtonPressed()) {
        if (UI.getCurrentScreen() == ScreenId::M_PERFORMANCE) {
            UI.toggleStopwatch(); // В спортивном режиме короткий клик запускает/останавливает таймер
        } else {
            UI.nextScreen();      // В остальных режимах переключает экран
        }
    }

    if (Sensors.isNextButtonHeld()) {
        if (UI.getCurrentScreen() == ScreenId::M_PERFORMANCE) {
            UI.resetStopwatch();  // Длинный клик в режиме M сбрасывает таймер
        } else {
            Sensors.resetVoltageExtremes(); // Сброс минимального пускового напряжения
        }
    }

    // 4. Обновление системного времени
    Time.update();

    // 5. Отрисовка графического интерфейса UI (каждые 50 мс, 20 FPS)
    if (currentMillis - lastUiUpdate >= 50) {
        lastUiUpdate = currentMillis;
        UI.update();
    }

    // 6. Периодическая проверка синхронизации времени по Wi-Fi (раз в час)
    if (Portal.isConnectedToStation() && (currentMillis - lastNtpCheck >= (NTP_SYNC_INTERVAL_SEC * 1000UL))) {
        lastNtpCheck = currentMillis;
        Time.syncNtp(DEFAULT_TIMEZONE_OFFSET, 0);
    }

    // Небольшая уступка квантов времени для FreeRTOS и Wi-Fi стека ESP32
    yield();
}
