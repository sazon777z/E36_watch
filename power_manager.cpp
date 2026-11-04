#include "power_manager.h"
#include <driver/rtc_io.h>
#include "can_driver.h"
#include "display_driver.h"
#include "trip_computer.h"
#include "ble_manager.h"
#include "ota_manager.h"
#include "ts_bridge.h"

PowerManager Power;

PowerManager::PowerManager()
    : lastActivityMs(0),
      wakeCause(ESP_SLEEP_WAKEUP_UNDEFINED),
      wakePins(0) {
}

void PowerManager::init() {
    wakeCause = esp_sleep_get_wakeup_cause();
    if (wakeCause == ESP_SLEEP_WAKEUP_EXT1) {
        wakePins = esp_sleep_get_ext1_wakeup_status();
    }

    // Возвращаем пины пробуждения в обычный цифровой режим, снимаем удержание CAN TX
    gpio_hold_dis((gpio_num_t)PIN_CAN_TX);
    rtc_gpio_deinit((gpio_num_t)PIN_CAN_RX);
    rtc_gpio_deinit((gpio_num_t)PIN_BTN_NEXT);

    lastActivityMs = millis();
    if (wokeFromSleep()) {
        Serial.printf("[POWER] Пробуждение из глубокого сна: %s\n", getWakeReasonStr());
    }
}

const char* PowerManager::getWakeReasonStr() const {
    if (wakeCause == ESP_SLEEP_WAKEUP_EXT1) {
        if (wakePins & (1ULL << PIN_CAN_RX)) return "CAN (зажигание)";
        if (wakePins & (1ULL << PIN_BTN_NEXT)) return "КНОПКА";
        return "EXT1";
    }
    if (wakeCause == ESP_SLEEP_WAKEUP_UNDEFINED) return "нет (обычный старт)";
    return "другое";
}

void PowerManager::update() {
#if SLEEP_ENABLED
    unsigned long now = millis();

    // Устройство активно, пока идут пакеты CAN, подключен телефон или идет обновление
    if (CanBus.isConnected() || BleMgr.isConnected() || Ota.isActive()) {
        lastActivityMs = now;
        return;
    }

    if (now - lastActivityMs >= SLEEP_IDLE_TIMEOUT_MS) {
        enterDeepSleep();
    }
#endif
}

void PowerManager::enterDeepSleep() {
    // Если на шине уже идет обмен или кнопка нажата - пробуждение случится сразу, не засыпаем
    if (digitalRead(PIN_BTN_NEXT) == LOW) {
        lastActivityMs = millis();
        return;
    }

    Serial.println("[POWER] CAN-шина молчит. Сохранение данных и переход в глубокий сон...");

    Trip.saveNow();
    TsBridge.shutdown();   // снять питание с FT232 ЭБУ
    CanBus.shutdown();
    Display.sleep();

    // CAN TX держим в "1" (рецессив): плавающий TX заставил бы трансивер выдать
    // доминантный бит на шину и тут же разбудить плату через собственный RX
    pinMode(PIN_CAN_TX, OUTPUT);
    digitalWrite(PIN_CAN_TX, HIGH);
    gpio_hold_en((gpio_num_t)PIN_CAN_TX);

    // Пробуждение по низкому уровню на CAN RX (доминантный бит) или на кнопке
    const uint64_t mask = (1ULL << PIN_CAN_RX) | (1ULL << PIN_BTN_NEXT);

    // Подтяжки к питанию держат пины в "1", пока шина молчит и кнопка отпущена
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
    rtc_gpio_init((gpio_num_t)PIN_CAN_RX);
    rtc_gpio_set_direction((gpio_num_t)PIN_CAN_RX, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pulldown_dis((gpio_num_t)PIN_CAN_RX);
    rtc_gpio_pullup_en((gpio_num_t)PIN_CAN_RX);
    rtc_gpio_init((gpio_num_t)PIN_BTN_NEXT);
    rtc_gpio_set_direction((gpio_num_t)PIN_BTN_NEXT, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pulldown_dis((gpio_num_t)PIN_BTN_NEXT);
    rtc_gpio_pullup_en((gpio_num_t)PIN_BTN_NEXT);

    esp_sleep_enable_ext1_wakeup(mask, ESP_EXT1_WAKEUP_ANY_LOW);

    Serial.flush();
    delay(50);
    esp_deep_sleep_start();
}
