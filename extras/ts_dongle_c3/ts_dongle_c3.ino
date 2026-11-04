/*
 * ============================================================================
 * ДОНГЛ TUNERSTUDIO ДЛЯ НОУТБУКА (ESP32-C3, например SuperMini)
 * ----------------------------------------------------------------------------
 * Вставляется в USB ноутбука и виден в Windows как обычный COM-порт.
 * Порт есть всегда, пока донгл в USB, поэтому TunerStudio сам восстанавливает
 * связь через 1–2 с после включения зажигания — как по кабелю.
 *
 *   TunerStudio --COM (USB)--> ESP32-C3 <~ESP-NOW~> дэшборд E36_watch (S3)
 *                                         --USB--> FT232 ЭБУ --> MegaSquirt 2
 *
 * Arduino IDE: плата "ESP32C3 Dev Module", USB CDC On Boot: Enabled,
 *              Core Debug Level: None.
 * Параметры пары ниже ДОЛЖНЫ совпадать с config.h дэшборда (TS_LINK_*).
 * Файл ts_link.h должен быть такой же, как в E36_watch.
 *
 * Светодиод (GPIO 8, синий на SuperMini):
 *   медленно мигает (1 раз в секунду)  — дэшборда не слышно;
 *   две короткие вспышки               — дэшборд на связи, ЭБУ (FT232) не подключен/выключен;
 *   горит постоянно                    — всё готово, можно работать в TunerStudio;
 *   короткие гашения                   — идет обмен данными.
 *
 * В этот COM-порт ничего не печатается: он целиком отдан TunerStudio.
 * ============================================================================
 */

#include "ts_link.h"
#include <freertos/stream_buffer.h>

// Ключи шифрования TS_LINK_PMK / TS_LINK_LMK — в secrets.h этой папки (не попадает в git).
// Они должны совпадать с secrets.h дэшборда. Образец: secrets.example.h.
#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "Нет secrets.h: скопируйте secrets.example.h в secrets.h и впишите ключи, как у дэшборда"
#endif

// ---- Параметры пары (как TS_LINK_CHANNEL / TS_LINK_NET_ID в config.h дэшборда) ----
#define LINK_CHANNEL   1
#define LINK_NET_ID    0xE36B0001UL

// ---- Железо ----
#define PIN_LED          8
#define LED_ACTIVE_LOW   1
// У SuperMini слабая антенна: на пониженной мощности связь обычно стабильнее
#define LINK_TX_POWER    WIFI_POWER_8_5dBm

static tslink::RadioLink radioLink;
static StreamBufferHandle_t toPc = nullptr;
static uint32_t lastActivityMs = 0;

static void onRadioRx(const uint8_t* d, size_t n) {
    xStreamBufferSend(toPc, d, n, 0);  // из задачи Wi-Fi: не ждем
}

static void ledWrite(bool on) {
    digitalWrite(PIN_LED, (on ^ (LED_ACTIVE_LOW != 0)) ? HIGH : LOW);
}

static void updateLed() {
    uint32_t now = millis();
    bool on;
    if (!radioLink.linkUp()) {
        on = (now % 1000) < 500;                       // медленное мигание
    } else if (!radioLink.peerReady()) {
        uint32_t p = now % 1000;                       // две короткие вспышки
        on = (p < 80) || (p >= 200 && p < 280);
    } else {
        on = (now - lastActivityMs) > 40 || (now % 60) < 30;  // горит, гаснет при обмене
    }
    ledWrite(on);
}

void setup() {
    pinMode(PIN_LED, OUTPUT);
    ledWrite(false);

    Serial.setRxBufferSize(4096);
    Serial.setTxBufferSize(4096);
    Serial.begin(115200);       // скорость для USB CDC не важна
    Serial.setTxTimeoutMs(20);  // не 0: в HWCDC Core 3.x при 0 возможна блокировка
    Serial.setDebugOutput(false);

    toPc = xStreamBufferCreate(4096, 1);
    radioLink.begin(tslink::ROLE_PC, LINK_NET_ID, LINK_CHANNEL, TS_LINK_PMK, TS_LINK_LMK,
                    onRadioRx, LINK_TX_POWER);
}

void loop() {
    radioLink.poll();

    // Ноутбук -> ЭБУ
    int av = Serial.available();
    if (av > 0) {
        uint8_t buf[tslink::MAX_PAYLOAD];
        size_t want = (size_t)av < sizeof(buf) ? (size_t)av : sizeof(buf);
        size_t n = Serial.read(buf, want);
        // Пока ЭБУ не готов, команды TunerStudio выбрасываем: он повторит их сам
        if (n > 0 && radioLink.peerReady()) {
            radioLink.send(buf, n);
            lastActivityMs = millis();
        }
    }

    // ЭБУ -> ноутбук
    uint8_t out[256];
    size_t m = toPc ? xStreamBufferReceive(toPc, out, sizeof(out), 0) : 0;
    if (m > 0) {
        Serial.write(out, m);
        lastActivityMs = millis();
    }

    updateLed();
    if (av <= 0 && m == 0) delay(1);
}
