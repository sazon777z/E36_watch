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
#include "gps_driver.h"
#include "trip_computer.h"
#include "time_keeper.h"
#include "sensors.h"
#include "warning_manager.h"
#include "ui_engine.h"
#include "ble_manager.h"

#include <esp_system.h>

// Тайминги выполнения задач
unsigned long lastSensorUpdate = 0;
unsigned long lastUiUpdate = 0;

static const char* getResetReasonStr(esp_reset_reason_t r) {
    switch (r) {
        case ESP_RST_POWERON:   return "POWER ON";
        case ESP_RST_EXT:       return "RST PIN (EN/RESET)";
        case ESP_RST_SW:        return "SW RESTART";
        case ESP_RST_PANIC:     return "PANIC / CRASH";
        case ESP_RST_INT_WDT:   return "INT WDT";
        case ESP_RST_TASK_WDT:  return "TASK WDT";
        case ESP_RST_WDT:       return "OTHER WDT";
        case ESP_RST_DEEPSLEEP: return "DEEP SLEEP";
        case ESP_RST_BROWNOUT:   return "BROWNOUT (POWER DIP/SHORT)";
        case ESP_RST_SDIO:      return "SDIO";
        case ESP_RST_USB:       return "USB RESET (DTR/UPLOAD)";
        case ESP_RST_JTAG:      return "JTAG RESET";
        case ESP_RST_EFUSE:     return "EFUSE ERROR";
        case ESP_RST_PWR_GLITCH:return "POWER GLITCH";
        case ESP_RST_CPU_LOCKUP:return "CPU LOCKUP";
        default:                return "UNKNOWN";
    }
}

void setup() {
    Serial.begin(115200);
    delay(200);

    esp_reset_reason_t rstReason = esp_reset_reason();
    const char* rstStr = getResetReasonStr(rstReason);
    Serial.printf("\n==================================================\n");
    Serial.printf("[BOOT] ПРИЧИНА ЗАГРУЗКИ: %d (%s)\n", (int)rstReason, rstStr);
    Serial.printf("==================================================\n");

    Serial.println("[BMW E36 OBC] Инициализация системы...");

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

    // 3. Инициализация GPS-модуля NEO-7M (UART1 на GPIO 44/43)
    Gps.init();

    // 4. Отображение заставки с причиной загрузки (RST reason)
    char splashMsg[64];
    snprintf(splashMsg, sizeof(splashMsg), "RST: %s", rstStr);
    UI.showBootSplash(splashMsg);

    // 5. Инициализация часов, сенсоров, одометра/расхода, варнингов, UI и Bluetooth LE
    Time.init();
    Trip.init();
    Sensors.init();
    Warnings.init();
    UI.init();
    BleMgr.init();

    Serial.println("[OK] Система готова к работе.");
}

void loop() {
    // 1. Постоянный неблокирующий прием пакетов CAN-шины MegaSquirt 2
    CanBus.update();

    // 2. Постоянное чтение и парсинг потока NMEA от GPS-модуля
    Gps.update();

    // 3. Расчет одометра и расхода топлива
    Trip.update();

    // 4. Обслуживание Bluetooth Low Energy (NUS сервис)
    BleMgr.update();

    unsigned long currentMillis = millis();

    // 5. Периодический опрос датчиков, варнингов и АЦП бортсети (каждые 20 мс для четкого считывания кнопки)
    if (currentMillis - lastSensorUpdate >= 20) {
        lastSensorUpdate = currentMillis;
        Sensors.update();
        Warnings.update(Sensors.getData());

        // Автоматическое диммирование при включении габаритов
        if (Sensors.getData().headlightsOn) {
            if (Display.getBrightness() > NIGHT_BRIGHTNESS) {
                Display.setBrightness(NIGHT_BRIGHTNESS);
            }
        }
    }

    // 6. Обработка команд через Serial (диагностика и стендовое тестирование)
    if (Serial.available()) {
        char c = Serial.read();
        if (c == 'n' || c == 'N') {
            Serial.println("[CMD] 'n' -> Следующий экран");
            UI.nextScreen();
        } else if (c == 'g' || c == 'G' || c == 'd' || c == 'D') {
            Serial.println("[CMD] 'g'/'d' -> Следующий тип датчика");
            UI.nextGaugeType();
        } else if (c == 'h' || c == 'H') {
            Serial.println("[CMD] 'h' -> Имитация удержания кнопки (HOLD)");
            if (UI.getCurrentScreen() == ScreenId::GAUGE_SELECTOR) {
                UI.setScreen(ScreenId::SINGLE_GAUGE);
            } else if (UI.getCurrentScreen() == ScreenId::SINGLE_GAUGE) {
                UI.setScreen(ScreenId::GAUGE_SELECTOR);
            } else if (UI.getCurrentScreen() == ScreenId::OBC_TRIP_FUEL) {
                Trip.resetTrip();
                UI.notifyTripReset();
            } else {
                Sensors.resetVoltageExtremes();
            }
        } else if (c == 'r' || c == 'R') {
            Serial.println("[CMD] 'r' -> Сброс одометра поездки");
            Trip.resetTrip();
            UI.notifyTripReset();
        } else if (c >= '1' && c <= '7') {
            ScreenId sid = (ScreenId)(c - '0');
            Serial.printf("[CMD] Прямой переход на экран %d\n", (int)sid);
            UI.setScreen(sid);
        }
    }

    // 7. Обработка нажатий физической кнопки (одиночный, двойной клик, удержание)
    ButtonAction btnAct = Sensors.getButtonAction();
    if (btnAct == ButtonAction::CLICK) {
        Serial.println("[BTN] Одиночный клик -> следующий экран");
        UI.nextScreen();
    } else if (btnAct == ButtonAction::DOUBLE_CLICK) {
        Serial.println("[BTN] Двойной клик -> переключение датчика");
        if (UI.getCurrentScreen() == ScreenId::GAUGE_SELECTOR || 
            UI.getCurrentScreen() == ScreenId::SINGLE_GAUGE) {
            UI.nextGaugeType();
        }
    } else if (btnAct == ButtonAction::HOLD) {
        Serial.println("[BTN] Удержание кнопки (>1.0 сек)");
        if (UI.getCurrentScreen() == ScreenId::GAUGE_SELECTOR) {
            Serial.println("[BTN] Разворачиваем датчик на весь экран");
            UI.setScreen(ScreenId::SINGLE_GAUGE);
        } else if (UI.getCurrentScreen() == ScreenId::SINGLE_GAUGE) {
            Serial.println("[BTN] Возврат в селектор приборов");
            UI.setScreen(ScreenId::GAUGE_SELECTOR);
        } else if (UI.getCurrentScreen() == ScreenId::OBC_TRIP_FUEL) {
            Serial.println("[BTN] Сброс суточного одометра");
            Trip.resetTrip();
            UI.notifyTripReset();
        } else {
            Sensors.resetVoltageExtremes();
        }
    }

    // 5. Обновление системного времени
    Time.update();

    // 6. Отрисовка графического интерфейса UI через LVGL (каждые 25 мс, 40 FPS)
    if (currentMillis - lastUiUpdate >= 25) {
        lastUiUpdate = currentMillis;
        UI.update();
    }

    // Небольшая уступка квантов времени для FreeRTOS и BLE стека ESP32
    yield();
}
