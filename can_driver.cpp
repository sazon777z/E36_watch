#include "can_driver.h"

CanDriver CanBus;

static inline uint16_t parseUint16BE(const uint8_t* b) {
    return ((uint16_t)b[0] << 8) | (uint16_t)b[1];
}

static inline int16_t parseInt16BE(const uint8_t* b) {
    return (int16_t)(((uint16_t)b[0] << 8) | (uint16_t)b[1]);
}

static inline float convertMs2Temp(int16_t raw) {
#if MS2_TEMP_IS_FAHR
    // Если в MS2 температура транслируется в 0.1 deg F:
    // (Fahrenheit - 32) * 5 / 9
    float degF = (float)raw * 0.1f;
    return (degF - 32.0f) * (5.0f / 9.0f);
#else
    // Если в 0.1 deg C:
    return (float)raw * 0.1f;
#endif
}

CanDriver::CanDriver() : isDriverInstalled(false) {
    memset(&telemetry, 0, sizeof(Ms2Telemetry));
    telemetry.batt_volt = 12.6f;
    telemetry.clt_c = 20.0f;
    telemetry.mat_c = 20.0f;
    telemetry.afr = 14.7f;
    telemetry.afr_target = 14.7f;
    telemetry.map_kpa = 100.0f;
    telemetry.boost_bar = 0.0f;
    telemetry.isOnline = false;
    telemetry.packetsTotal = 0;
    telemetry.lastPacketMs = 0;
}

bool CanDriver::init() {
    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(
        (gpio_num_t)PIN_CAN_TX,
        (gpio_num_t)PIN_CAN_RX,
        TWAI_MODE_NORMAL
    );
    // Допустимо принимать все стандартные сообщения
    twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    esp_err_t err = twai_driver_install(&g_config, &t_config, &f_config);
    if (err != ESP_OK) {
        Serial.printf("[CAN ERROR] Не удалось установить TWAI драйвер: 0x%x\n", err);
        return false;
    }

    err = twai_start();
    if (err != ESP_OK) {
        Serial.printf("[CAN ERROR] Не удалось запустить TWAI контроллер: 0x%x\n", err);
        return false;
    }

    isDriverInstalled = true;
    Serial.printf("[CAN OK] TWAI инициализирован на скорости %d kbps (TX: GPIO %d, RX: GPIO %d)\n",
                  CAN_SPEED_KBPS, PIN_CAN_TX, PIN_CAN_RX);
    return true;
}

void CanDriver::update() {
    if (!isDriverInstalled) return;

    twai_message_t msg;
    // Считываем все накопившиеся в очереди CAN-пакеты (неблокирующий опрос)
    while (twai_receive(&msg, 0) == ESP_OK) {
        parsePacket(msg);
    }

    // Проверка таймаута связи с ЭБУ MegaSquirt 2
    if (telemetry.isOnline && (millis() - telemetry.lastPacketMs > 2500)) {
        telemetry.isOnline = false;
        Serial.println("[CAN WARN] Таймаут связи с MegaSquirt 2 (OFFLINE)");
    }
}

void CanDriver::parsePacket(const twai_message_t& msg) {
    if (msg.rtr) return; // Игнорируем RTR запросы

    uint32_t id = msg.identifier;
    const uint8_t* d = msg.data;
    uint8_t len = msg.data_length_code;

    // Проверяем диапазон Realtime Broadcasting (1520..1524)
    if (id >= MS2_BASE_ID_ADV && id <= (MS2_BASE_ID_ADV + 5)) {
        parseMs2RealtimeAdv(id, d, len);
    }
    // Проверяем диапазон Simplified Dash Broadcasting (1512..1514)
    else if (id >= MS2_BASE_ID_DASH && id <= (MS2_BASE_ID_DASH + 3)) {
        parseMs2Dash(id, d, len);
    }
}

void CanDriver::parseMs2RealtimeAdv(uint32_t canId, const uint8_t* d, uint8_t len) {
    if (len < 8) return;

    telemetry.isOnline = true;
    telemetry.lastPacketMs = millis();
    telemetry.packetsTotal++;

    switch (canId) {
        case 1520: // Группа 0: время работы, впрыск, обороты
            telemetry.pw1_us = parseUint16BE(d + 2);
            telemetry.rpm = parseUint16BE(d + 6);
            break;

        case 1521: // Группа 1: УОЗ, флаги, целевой AFR
            telemetry.advance_deg = (float)parseInt16BE(d) * 0.1f;
            telemetry.engine_flags = d[3];
            telemetry.afr_target = (float)parseUint16BE(d + 4) * 0.1f;
            break;

        case 1522: // Группа 2: барометр, MAP, MAT, CLT
            telemetry.map_kpa = (float)parseInt16BE(d + 2) * 0.1f;
            telemetry.boost_bar = (telemetry.map_kpa - 100.0f) / 100.0f;
            telemetry.mat_c = convertMs2Temp(parseInt16BE(d + 4));
            telemetry.clt_c = convertMs2Temp(parseInt16BE(d + 6));
            break;

        case 1523: // Группа 3: Дроссель, Напряжение АКБ, ШЛЗ AFR
            telemetry.tps_pct = (float)parseInt16BE(d) * 0.1f;
            telemetry.batt_volt = (float)parseInt16BE(d + 2) * 0.1f;
            telemetry.afr = (float)parseInt16BE(d + 4) * 0.1f;
            break;

        case 1524: // Группа 4: Детонация, EGT, Скорость
            telemetry.speed_kmh = (uint16_t)((float)parseUint16BE(d + 4) * 0.1f);
            break;
    }
}

void CanDriver::parseMs2Dash(uint32_t canId, const uint8_t* d, uint8_t len) {
    if (len < 8) return;

    telemetry.isOnline = true;
    telemetry.lastPacketMs = millis();
    telemetry.packetsTotal++;

    switch (canId) {
        case 1512: // MAP, RPM, CLT, TPS
            telemetry.map_kpa = (float)parseInt16BE(d) * 0.1f;
            telemetry.boost_bar = (telemetry.map_kpa - 100.0f) / 100.0f;
            telemetry.rpm = parseUint16BE(d + 2);
            telemetry.clt_c = convertMs2Temp(parseInt16BE(d + 4));
            telemetry.tps_pct = (float)parseInt16BE(d + 6) * 0.1f;
            break;

        case 1513: // MAT, УОЗ, AFR, Батарея
            telemetry.mat_c = convertMs2Temp(parseInt16BE(d));
            telemetry.advance_deg = (float)parseInt16BE(d + 2) * 0.1f;
            telemetry.afr = (float)parseInt16BE(d + 4) * 0.1f;
            telemetry.batt_volt = (float)parseInt16BE(d + 6) * 0.1f;
            break;

        case 1514: // VSS Скорость
            telemetry.speed_kmh = (uint16_t)((float)parseUint16BE(d) * 0.1f);
            break;
    }
}
