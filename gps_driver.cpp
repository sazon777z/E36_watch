#include "gps_driver.h"
#include "time_keeper.h"

GpsDriver Gps;
static HardwareSerial GpsSerial(1);

GpsDriver::GpsDriver() : bufIdx(0), lastRtcSyncMillis(0) {
    memset(&data, 0, sizeof(GpsData));
    data.speed_kmh = 0.0f;
    data.hasFix = false;
    data.timeValid = false;
    data.timeSyncedToRtc = false;
}

bool GpsDriver::init() {
    Serial.printf("[GPS] Инициализация NEO-7M: RX=GPIO %d, TX=GPIO %d @ %d baud\n",
                  PIN_GPS_RX, PIN_GPS_TX, GPS_BAUDRATE);
    GpsSerial.begin(GPS_BAUDRATE, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
    return true;
}

bool GpsDriver::isConnected() const {
    return (millis() - data.lastSentenceMs < 3000) && (data.sentencesParsed > 0);
}

void GpsDriver::update() {
    while (GpsSerial.available() > 0) {
        char c = (char)GpsSerial.read();

        if (c == '$') {
            bufIdx = 0;
            buffer[bufIdx++] = c;
        } else if (c == '\r' || c == '\n') {
            if (bufIdx > 10) {
                buffer[bufIdx] = '\0';
                parseSentence(buffer);
            }
            bufIdx = 0;
        } else {
            if (bufIdx < (sizeof(buffer) - 1)) {
                buffer[bufIdx++] = c;
            } else {
                bufIdx = 0; // Переполнение буфера
            }
        }
    }

    // Проверка таймаута фиксации спутников
    if (data.hasFix && (millis() - data.lastFixMs > 4000)) {
        data.hasFix = false;
        data.speed_kmh = 0.0f;
    }
}

bool GpsDriver::verifyChecksum(const char* s) {
    if (s[0] != '$') return false;
    
    uint8_t calcXor = 0;
    int i = 1;
    while (s[i] != '\0' && s[i] != '*') {
        calcXor ^= (uint8_t)s[i];
        i++;
    }

    if (s[i] == '*') {
        uint8_t expected = (uint8_t)strtoul(&s[i + 1], NULL, 16);
        return (calcXor == expected);
    }
    return true; // Если контрольная сумма отсутствует, не отбрасываем
}

void GpsDriver::parseSentence(char* sentence) {
    if (!verifyChecksum(sentence)) return;

    data.lastSentenceMs = millis();
    data.sentencesParsed++;

    // Разбиваем строку NMEA с сохранением пустых полей (,,)
    char* fields[24];
    int fCount = 0;
    fields[fCount++] = sentence;

    for (int i = 0; sentence[i] != '\0' && fCount < 24; i++) {
        if (sentence[i] == ',' || sentence[i] == '*') {
            sentence[i] = '\0';
            fields[fCount++] = &sentence[i + 1];
        }
    }

    if (fCount < 2) return;

    // Поддержка $GPRMC (GPS) и $GNRMC (GPS+ГЛОНАСС)
    if (strstr(fields[0], "RMC") != NULL) {
        parseRMC(fields, fCount);
    }
    // Поддержка $GPGGA и $GNGGA
    else if (strstr(fields[0], "GGA") != NULL) {
        parseGGA(fields, fCount);
    }
}

void GpsDriver::parseRMC(char* f[], int count) {
    if (count < 10) return;

    // f[1]: Время UTC (hhmmss.sss)
    if (strlen(f[1]) >= 6) {
        data.hour = (f[1][0] - '0') * 10 + (f[1][1] - '0');
        data.minute = (f[1][2] - '0') * 10 + (f[1][3] - '0');
        data.second = (f[1][4] - '0') * 10 + (f[1][5] - '0');
        data.timeValid = true;
    }

    // f[2]: Статус 'A' = валидно, 'V' = предупреждение
    bool valid = (f[2][0] == 'A');
    data.hasFix = valid;

    if (valid) {
        data.lastFixMs = millis();

        // f[7]: Скорость в узлах (Knots) -> переводим в км/ч (* 1.852)
        if (strlen(f[7]) > 0) {
            float knots = atof(f[7]);
            float kmh = knots * 1.852f;
            // Порог фильтрации шума GPS на стоянке
            data.speed_kmh = (kmh < 1.8f) ? 0.0f : kmh;
        }

        // f[9]: Дата (ddmmyy)
        if (strlen(f[9]) >= 6) {
            data.day = (f[9][0] - '0') * 10 + (f[9][1] - '0');
            data.month = (f[9][2] - '0') * 10 + (f[9][3] - '0');
            data.year = 2000 + (f[9][4] - '0') * 10 + (f[9][5] - '0');
        }

        // Автосинхронизация RTC часов при первом фиксе или раз в 60 сек
        if (!data.timeSyncedToRtc || (millis() - lastRtcSyncMillis >= 60000UL)) {
            syncRtcTime();
        }
    }
}

void GpsDriver::parseGGA(char* f[], int count) {
    if (count < 8) return;

    // f[6]: Качество фиксации (1 = GPS fix, 2 = DGPS fix)
    int fixQuality = atoi(f[6]);
    if (fixQuality > 0) {
        data.hasFix = true;
        data.lastFixMs = millis();
    }

    // f[7]: Число видимых спутников в решении
    data.satellites = (uint8_t)atoi(f[7]);

    // f[9]: Высота над уровнем моря (м)
    if (count > 9 && strlen(f[9]) > 0) {
        data.altitude_m = atof(f[9]);
    }
}

void GpsDriver::syncRtcTime() {
    if (!data.timeValid || data.year < 2024) return;

    // Преобразуем UTC время со спутника с учетом часового пояса
    int h = data.hour + DEFAULT_TIMEZONE_OFFSET;
    int d = data.day;
    int m = data.month;
    int y = data.year;

    if (h >= 24) {
        h -= 24;
        d += 1;
        // Упрощенный переход через границу месяца для типовых месяцев
        if (d > 30) {
            d = 1;
            m += 1;
            if (m > 12) {
                m = 1;
                y += 1;
            }
        }
    } else if (h < 0) {
        h += 24;
        d -= 1;
        if (d < 1) {
            d = 28;
            m -= 1;
            if (m < 1) {
                m = 12;
                y -= 1;
            }
        }
    }

    Time.setManualTime(y, m, d, h, data.minute, data.second);
    data.timeSyncedToRtc = true;
    lastRtcSyncMillis = millis();

    Serial.printf("[GPS] Точное время синхронизировано со спутников: %02d:%02d:%02d (%02d.%02d.%04d)\n",
                  h, data.minute, data.second, d, m, y);
}
