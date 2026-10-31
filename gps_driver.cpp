#include "gps_driver.h"
#include "time_keeper.h"

GpsDriver Gps;
static HardwareSerial GpsSerial(1);

GpsDriver::GpsDriver() : bufIdx(0), lastRtcSyncMillis(0), moveConfirmCount(0) {
    memset(&data, 0, sizeof(GpsData));
    data.hdop = 99.0f;
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
        data.moving = false;
        moveConfirmCount = 0;
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

    // NEO-7M всегда передает контрольную сумму: строки без нее (обрыв/помеха) отбрасываем
    if (s[i] != '*' || !isxdigit((unsigned char)s[i + 1]) || !isxdigit((unsigned char)s[i + 2])) {
        return false;
    }
    uint8_t expected = (uint8_t)strtoul(&s[i + 1], NULL, 16);
    return (calcXor == expected);
}

// Разбор двух десятичных цифр; -1 при ошибке формата
static int parse2(const char* p) {
    if (!isdigit((unsigned char)p[0]) || !isdigit((unsigned char)p[1])) return -1;
    return (p[0] - '0') * 10 + (p[1] - '0');
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
        int hh = parse2(f[1]), mm = parse2(f[1] + 2), ss = parse2(f[1] + 4);
        if (hh >= 0 && hh <= 23 && mm >= 0 && mm <= 59 && ss >= 0 && ss <= 60) {
            data.hour = hh;
            data.minute = mm;
            data.second = (ss == 60) ? 59 : ss; // високосная секунда
            data.timeValid = true;
        }
    }

    // f[2]: Статус 'A' = валидно, 'V' = предупреждение
    bool valid = (f[2][0] == 'A');
    data.hasFix = valid;

    if (valid) {
        data.lastFixMs = millis();

        // f[7]: Скорость в узлах (Knots) -> переводим в км/ч (* 1.852)
        if (strlen(f[7]) > 0) {
            float knots = atof(f[7]);
            data.rawSpeed_kmh = knots * 1.852f;
            data.speed_kmh = filterSpeed(data.rawSpeed_kmh);
        }

        // f[9]: Дата (ddmmyy)
        if (strlen(f[9]) >= 6) {
            int dd = parse2(f[9]), mo = parse2(f[9] + 2), yy = parse2(f[9] + 4);
            if (dd >= 1 && dd <= 31 && mo >= 1 && mo <= 12 && yy >= 0) {
                data.day = dd;
                data.month = mo;
                data.year = 2000 + yy;
            }
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

    // f[8]: HDOP (точность в плане)
    data.hdop = (strlen(f[8]) > 0) ? atof(f[8]) : 99.0f;

    // f[9]: Высота над уровнем моря (м)
    if (count > 9 && strlen(f[9]) > 0) {
        data.altitude_m = atof(f[9]);
    }
}

// Фильтр стоянки: отсекает дрейф скорости при неподвижном автомобиле (вызывается раз в фикс RMC)
float GpsDriver::filterSpeed(float kmh) {
    bool goodSignal = (data.satellites >= GPS_MIN_SATELLITES) && (data.hdop <= GPS_MAX_HDOP);

    if (data.moving) {
        // В движении: останавливаемся при малой скорости или потере качества приема
        if (kmh < GPS_MOVE_STOP_KMH || !goodSignal) {
            data.moving = false;
            moveConfirmCount = 0;
        }
    } else {
        // На стоянке: движение подтверждаем только N фиксов подряд выше порога
        if (goodSignal && kmh >= GPS_MOVE_START_KMH) {
            if (++moveConfirmCount >= GPS_MOVE_CONFIRM_FIXES) {
                data.moving = true;
            }
        } else {
            moveConfirmCount = 0;
        }
    }

    return data.moving ? kmh : 0.0f;
}

void GpsDriver::syncRtcTime() {
    if (!data.timeValid || data.year < 2024) return;

    // Преобразуем UTC время со спутника с учетом текущего часового пояса
    int tz = Time.getTimezoneOffset();
    int h = data.hour + tz;
    int d = data.day;
    int m = data.month;
    int y = data.year;

    while (h >= 24) {
        h -= 24;
        d += 1;
        int daysInMonth = 31;
        if (m == 4 || m == 6 || m == 9 || m == 11) daysInMonth = 30;
        else if (m == 2) daysInMonth = (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) ? 29 : 28;
        if (d > daysInMonth) {
            d = 1;
            m += 1;
            if (m > 12) {
                m = 1;
                y += 1;
            }
        }
    }
    while (h < 0) {
        h += 24;
        d -= 1;
        if (d < 1) {
            m -= 1;
            if (m < 1) {
                m = 12;
                y -= 1;
            }
            int daysInMonth = 31;
            if (m == 4 || m == 6 || m == 9 || m == 11) daysInMonth = 30;
            else if (m == 2) daysInMonth = (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) ? 29 : 28;
            d = daysInMonth;
        }
    }

    Time.setManualTime(y, m, d, h, data.minute, data.second);
    data.timeSyncedToRtc = true;
    lastRtcSyncMillis = millis();

    Serial.printf("[GPS] Точное время синхронизировано со спутников: %02d:%02d:%02d (UTC%+d, %02d.%02d.%04d)\n",
                  h, data.minute, data.second, tz, d, m, y);
}
