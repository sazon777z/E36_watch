/*
 * ============================================================================
 * ДИАГНОСТИКА РАДИОКАНАЛА TunerStudio (донгл ESP32-C3, проверка на столе)
 * ----------------------------------------------------------------------------
 * Прошивается в донгл ВМЕСТО обычной (extras/ts_dongle_c3), когда связи нет и
 * непонятно почему. Раз в секунду печатает в COM-порт, что донгл слышит от
 * дэшборда. TunerStudio с этой прошивкой не работает: после проверки верните
 * обычную прошивку донгла.
 *
 * Arduino IDE: плата "ESP32C3 Dev Module", USB CDC On Boot: Enabled,
 *              Core Debug Level: None. Монитор порта: 115200.
 * secrets.h — скопируйте из extras/ts_dongle_c3 (те же ключи).
 * ts_link.h — копия общего файла из корня проекта (не менять отдельно).
 * Читаете порт своей программой — держите DTR=1, RTS=0: при DTR=0/RTS=1
 * USB-JTAG перезагружает C3.
 *
 * Строка раз в секунду:
 *   t=<с> rx=<кадров ESP-NOW принято> hello=<HELLO отправлено>/<ошибок>
 *   rssi=<уровень последнего кадра ESP-NOW, dBm> src=<его отправитель>
 *   peer=<MAC соседа или "-"> up=<связь 0/1> ecu=<FT232 ЭБУ готов 0/1>
 *
 * Как читать:
 *   rx не растет           — дэшборд не в эфире: статус на экране «Система»
 *                            (NOMEM / ERR WIFI), или другой канал (LINK_CHANNEL);
 *   rx растет, peer "-"    — кадры есть, но не от нашей пары (LINK_NET_ID);
 *   peer есть, up=0        — дэшборд не слышит донгл (далеко, помехи);
 *   up=1                   — связь есть (дэшборд: TS PC). Ключи шифрования
 *                            проверяются только данными: если up=1, а TunerStudio
 *                            не получает ответа, сверьте secrets.h обеих прошивок;
 *   ecu=1                  — FT232 ЭБУ подключен (дэшборд: TS LINK).
 * ============================================================================
 */

#include "ts_link.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "Нет secrets.h: скопируйте secrets.h из extras/ts_dongle_c3"
#endif

// ---- Как в обычной прошивке донгла и в config.h дэшборда (TS_LINK_*) ----
#define LINK_CHANNEL   1
#define LINK_NET_ID    0xE36B0001UL
#define LINK_TX_POWER  WIFI_POWER_8_5dBm

#define PIN_LED          8
#define LED_ACTIVE_LOW   1

static tslink::RadioLink radioLink;
static void onRadioRx(const uint8_t*, size_t) {}

// Уровень сигнала и отправитель последнего кадра ESP-NOW. ts_link.h их не отдает,
// поэтому смотрим эфир в режиме прослушки: ESP-NOW — это кадры управления
// "action", категория 127 (vendor specific) с OUI Espressif 18:fe:34.
static volatile int s_rssi = 0;
static uint8_t s_src[6] = {0};
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

static void onPromisc(void* buf, wifi_promiscuous_pkt_type_t type) {
    if (type != WIFI_PKT_MGMT) return;
    const wifi_promiscuous_pkt_t* p = (const wifi_promiscuous_pkt_t*)buf;
    const uint8_t* f = p->payload;
    if (p->rx_ctrl.sig_len < 28 || f[0] != 0xD0) return;  // не кадр action
    if (f[24] != 127 || f[25] != 0x18 || f[26] != 0xFE || f[27] != 0x34) return;
    portENTER_CRITICAL(&s_mux);
    s_rssi = p->rx_ctrl.rssi;
    memcpy(s_src, f + 10, 6);  // addr2 — отправитель
    portEXIT_CRITICAL(&s_mux);
}

// MAC соседа: единственный адресат ESP-NOW, кроме широковещательного
static bool findPeer(uint8_t mac[6]) {
    esp_now_peer_info_t p = {};
    for (bool first = true; esp_now_fetch_peer(first, &p) == ESP_OK; first = false) {
        if (p.peer_addr[0] != 0xFF) {
            memcpy(mac, p.peer_addr, 6);
            return true;
        }
    }
    return false;
}

static void printMac(const uint8_t* m) {
    Serial.printf("%02x:%02x:%02x:%02x:%02x:%02x", m[0], m[1], m[2], m[3], m[4], m[5]);
}

static void ledWrite(bool on) {
    digitalWrite(PIN_LED, (on ^ (LED_ACTIVE_LOW != 0)) ? HIGH : LOW);
}

void setup() {
    pinMode(PIN_LED, OUTPUT);
    ledWrite(false);
    Serial.begin(115200);
    delay(1500);  // успеть открыть монитор порта

    bool ok = radioLink.begin(tslink::ROLE_PC, LINK_NET_ID, LINK_CHANNEL, TS_LINK_PMK, TS_LINK_LMK,
                              onRadioRx, LINK_TX_POWER);
    wifi_promiscuous_filter_t flt = {};
    flt.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT;
    esp_wifi_set_promiscuous_filter(&flt);
    esp_wifi_set_promiscuous_rx_cb(onPromisc);
    esp_wifi_set_promiscuous(true);

    uint8_t ch = 0, mac[6] = {0};
    wifi_second_chan_t sc;
    esp_wifi_get_channel(&ch, &sc);
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    Serial.printf("DIAG begin=%d ch=%u net=%08lX myMac=", ok, ch, (unsigned long)LINK_NET_ID);
    printMac(mac);
    Serial.println();
}

void loop() {
    radioLink.poll();
    uint32_t now = millis();

    static uint32_t lastPrint = 0;
    if (now - lastPrint >= 1000) {
        lastPrint = now;
        int rssi;
        uint8_t src[6], peer[6];
        portENTER_CRITICAL(&s_mux);
        rssi = s_rssi;
        memcpy(src, s_src, 6);
        portEXIT_CRITICAL(&s_mux);

        Serial.printf("t=%lu rx=%lu hello=%lu/%lu rssi=%d src=", (unsigned long)(now / 1000),
                      (unsigned long)radioLink.statRawRx, (unsigned long)radioLink.statHelloTx,
                      (unsigned long)radioLink.statHelloErr, rssi);
        printMac(src);
        Serial.print(" peer=");
        if (radioLink.hasPeerNow() && findPeer(peer)) printMac(peer);
        else Serial.print("-");
        Serial.printf(" up=%d ecu=%d\n", (int)radioLink.linkUp(), (int)radioLink.peerReady());
    }

    // Светодиод: горит — связь есть, мигает — нет
    ledWrite(radioLink.linkUp() || (now % 1000) < 500);
    delay(2);
}
