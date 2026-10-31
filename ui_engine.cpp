#include "ui_engine.h"
#include "ble_manager.h"
#include "can_driver.h"
#include "ota_manager.h"
#include <Preferences.h>
#include <stdarg.h>
#include <Fonts/FreeSansBoldOblique12pt7b.h>

extern "C" {
    extern const lv_font_t lv_font_clock_96;
}

UiEngine UI;

// =============================================================================
// ПАЛИТРА: черный фон, янтарная подсветка в духе приборки E36, ///M акценты
// =============================================================================
#define C_BG          lv_color_hex(0x000000)
#define C_SURFACE     lv_color_hex(0x101216)
#define C_SURFACE2    lv_color_hex(0x1C2027)
#define C_TEXT        lv_color_hex(0xFFFFFF)
#define C_DIM         lv_color_hex(0x7D848F)
#define C_ACCENT      lv_color_hex(0xFF8A1F)   // янтарный (подсветка E36)
#define C_M_BLUE      lv_color_hex(0x1C69D4)
#define C_M_LBLUE     lv_color_hex(0x4FA3FF)
#define C_M_RED       lv_color_hex(0xE11E28)
#define C_OK          lv_color_hex(0x19D27A)
#define C_ALARM_BG    lv_color_hex(0xC8141E)

static const int CONTENT_TOP = 26;     // Под верхней строкой статуса
static const uint32_t DATA_PERIOD_MS = 33;   // Обновление данных и сглаживания ~30 Гц
static const uint32_t SCREEN_ANIM_MS = 220;  // Длительность сдвига экрана

// Захват кадра в монитор порта
static volatile bool g_capture = false;

// USB CDC может принять меньше, чем просили (буфер полон) - дописываем остаток
static void serialWriteAll(const uint8_t* p, size_t len) {
    unsigned long start = millis();
    while (len > 0 && millis() - start < 3000) {
        size_t n = Serial.write(p, len > 256 ? 256 : len);
        if (n == 0) {
            delay(1);
            continue;
        }
        p += n;
        len -= n;
    }
}

// =============================================================================
// ВСПОМОГАТЕЛЬНЫЕ СТРУКТУРЫ
// =============================================================================
float SmoothValue::step(float target, float dtSec, float tauSec) {
    if (!primed) {
        snap(target);
        return value;
    }
    float k = 1.0f - expf(-dtSec / tauSec);
    value += (target - value) * k;
    return value;
}

void CachedLabel::set(const char* text) {
    if (!obj) return;
    if (initialized && strncmp(last, text, sizeof(last) - 1) == 0) return;
    initialized = true;
    strncpy(last, text, sizeof(last) - 1);
    last[sizeof(last) - 1] = '\0';
    lv_label_set_text(obj, text);
}

void CachedLabel::setf(const char* fmt, ...) {
    char buf[sizeof(last)];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    set(buf);
}

void Tile::setBar(int32_t v) {
    if (!bar || v == lastBar) return;
    lastBar = v;
    lv_bar_set_value(bar, v, LV_ANIM_OFF);
}

void Tile::setAlarm(bool alarm, bool blinkPhase) {
    if (!box) return;
    uintptr_t target = alarm ? (blinkPhase ? 2 : 1) : 0;
    if (target == alarmState) return;
    alarmState = target;
    // 2 = красная заливка, 1 = красная рамка (пауза мигания), 0 = норма
    lv_obj_set_style_bg_color(box, target == 2 ? C_ALARM_BG : C_SURFACE, 0);
    lv_obj_set_style_border_width(box, target ? 2 : 0, 0);
    lv_obj_set_style_border_color(box, C_M_RED, 0);
    if (bar) {
        lv_obj_set_style_bg_color(bar, alarm ? C_TEXT : C_ACCENT, LV_PART_INDICATOR);
    }
}

// =============================================================================
// ПОСТРОИТЕЛИ ВИДЖЕТОВ
// =============================================================================
static lv_obj_t* makeBox(lv_obj_t* parent, int x, int y, int w, int h, lv_color_t color, int radius) {
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, color, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return o;
}

static lv_obj_t* makeLabel(lv_obj_t* parent, const lv_font_t* font, lv_color_t color, const char* text) {
    lv_obj_t* l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_label_set_text(l, text);
    return l;
}

static lv_obj_t* makeCaption(lv_obj_t* parent, const char* text) {
    lv_obj_t* l = makeLabel(parent, &lv_font_montserrat_12, C_ACCENT, text);
    lv_obj_set_style_text_letter_space(l, 1, 0);
    return l;
}

static lv_obj_t* makeBar(lv_obj_t* parent, int x, int y, int w, int h, int32_t minV, int32_t maxV) {
    lv_obj_t* b = lv_bar_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, w, h);
    lv_obj_set_style_bg_color(b, C_SURFACE2, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(b, h / 2, 0);
    lv_obj_set_style_bg_color(b, C_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(b, h / 2, LV_PART_INDICATOR);
    lv_bar_set_range(b, minV, maxV);
    lv_bar_set_value(b, minV, LV_ANIM_OFF);
    return b;
}

// Плитка: заголовок слева, единица справа, крупное значение, подпись или мини-шкала снизу
static void makeTile(Tile& t, lv_obj_t* parent, int x, int y, int w, int h,
                     const char* caption, const char* unit, int valueY,
                     bool withSub, int32_t barMin, int32_t barMax) {
    t.box = makeBox(parent, x, y, w, h, C_SURFACE, 10);

    lv_obj_t* cap = makeCaption(t.box, caption);
    lv_obj_set_pos(cap, 10, 8);

    if (unit && unit[0]) {
        lv_obj_t* u = makeLabel(t.box, &lv_font_montserrat_12, C_DIM, unit);
        lv_obj_align(u, LV_ALIGN_TOP_RIGHT, -10, 8);
    }

    t.value.obj = makeLabel(t.box, &lv_font_telemetry_24, C_TEXT, "--");
    lv_obj_set_pos(t.value.obj, 10, valueY);

    if (withSub) {
        t.sub.obj = makeLabel(t.box, &lv_font_montserrat_12, C_DIM, "");
        lv_obj_align(t.sub.obj, LV_ALIGN_BOTTOM_LEFT, 10, -8);
    }
    if (barMin != barMax) {
        t.bar = makeBar(t.box, 10, h - 14, w - 20, 5, barMin, barMax);
    }
}

static lv_obj_t* makeScreen() {
    lv_obj_t* s = lv_obj_create(NULL);
    lv_obj_remove_style_all(s);
    lv_obj_set_style_bg_color(s, C_BG, 0);
    lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s, LV_OBJ_FLAG_SCROLLABLE);
    return s;
}

static int32_t clampI(int32_t v, int32_t lo, int32_t hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// =============================================================================
// ДРАЙВЕР ДИСПЛЕЯ
// =============================================================================
UiEngine::UiEngine()
    : currentScreen(ScreenId::CLASSIC_CLOCK),
      currentGauge(GaugeType::BOOST),
      lastDataMs(0),
      tripResetNoticeMs(0),
      clockColon(nullptr),
      colonShown(-1),
      ms2Dot(nullptr),
      tripInstBar(nullptr),
      tripInstBarLast(INT32_MIN),
      shiftLit(-1),
      shiftFlash(false),
      gaugeArc(nullptr),
      gaugeArcLast(INT32_MIN),
      gaugeAlarmState(0xFF) {
    memset(screens, 0, sizeof(screens));
    memset(pageDots, 0, sizeof(pageDots));
    memset(shiftSeg, 0, sizeof(shiftSeg));
}

void UiEngine::dispFlushCb(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* color_p) {
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);

    Adafruit_ST7789& tft = Display.getTft();
    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    // bigEndian = true, так как в lv_conf.h активирован LV_COLOR_16_SWAP 1
    tft.writePixels((uint16_t*)color_p, w * h, true, true);
    tft.endWrite();

    if (g_capture) {
        // Формат: '@' x1 y1 w h (uint16 LE) + пиксели RGB565 big-endian
        uint8_t hdr[9] = { '@',
                           (uint8_t)area->x1, (uint8_t)(area->x1 >> 8), (uint8_t)area->y1, (uint8_t)(area->y1 >> 8),
                           (uint8_t)w, (uint8_t)(w >> 8), (uint8_t)h, (uint8_t)(h >> 8) };
        serialWriteAll(hdr, sizeof(hdr));
        serialWriteAll((const uint8_t*)color_p, w * h * 2);
    }

    lv_disp_flush_ready(drv);
}

void UiEngine::captureScreen() {
#if ARDUINO_USB_CDC_ON_BOOT
    Serial.setTxTimeoutMs(500);
#endif
    Serial.flush();
    Serial.print("\nCAPSTART\n");
    g_capture = true;
    lv_obj_invalidate(lv_scr_act());
    lv_obj_invalidate(lv_layer_top());
    lv_refr_now(NULL);
    g_capture = false;
    Serial.print("\nCAPEND\n");
    Serial.flush();
#if ARDUINO_USB_CDC_ON_BOOT
    Serial.setTxTimeoutMs(10);
#endif
}

// =============================================================================
// ИНИЦИАЛИЗАЦИЯ
// =============================================================================
void UiEngine::init() {
    lv_init();

    lv_disp_draw_buf_init(&draw_buf, buf_1, NULL, 320 * LV_BUF_LINES);

    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = 320;
    disp_drv.ver_res = 240;
    disp_drv.flush_cb = dispFlushCb;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    loadGaugeFromNvs();

    createScreenClock();
    createScreenTrip();
    createScreenTelem();
    createScreenMPerf();
    createScreenGauge();
    createScreenSettings();
    createTopLayer();

    currentScreen = ScreenId::CLASSIC_CLOCK;
    lv_scr_load(screens[(int)ScreenId::CLASSIC_CLOCK]);
    updateTopLayer();
    updateGaugeScreen(true);
    lastDataMs = millis();
}

// -----------------------------------------------------------------------------
// Верхняя строка статуса и индикатор страниц - общие для всех экранов
// -----------------------------------------------------------------------------
void UiEngine::createTopLayer() {
    lv_obj_t* top = lv_layer_top();

    ms2Dot = makeBox(top, 12, 9, 8, 8, C_SURFACE2, LV_RADIUS_CIRCLE);
    ms2Text.obj = makeLabel(top, &lv_font_montserrat_12, C_DIM, "MS2");
    lv_obj_set_pos(ms2Text.obj, 25, 5);

    titleLbl.obj = makeLabel(top, &lv_font_montserrat_12, C_TEXT, "");
    lv_obj_set_style_text_letter_space(titleLbl.obj, 2, 0);
    lv_obj_align(titleLbl.obj, LV_ALIGN_TOP_MID, 0, 5);

    gpsLbl.obj = makeLabel(top, &lv_font_montserrat_12, C_DIM, LV_SYMBOL_GPS " --");
    lv_obj_align(gpsLbl.obj, LV_ALIGN_TOP_RIGHT, -12, 5);

    // Точки-индикаторы экранов внизу по центру
    const int n = (int)ScreenId::COUNT - 1;
    const int gap = 10;
    int x0 = 160 - ((n - 1) * gap) / 2 - 2;
    for (int i = 0; i < n; i++) {
        pageDots[i] = makeBox(top, x0 + i * gap, 231, 4, 4, C_SURFACE2, LV_RADIUS_CIRCLE);
    }
}

// -----------------------------------------------------------------------------
// ЭКРАН 1: ЧАСЫ
// -----------------------------------------------------------------------------
void UiEngine::createScreenClock() {
    lv_obj_t* s = makeScreen();
    screens[(int)ScreenId::CLASSIC_CLOCK] = s;

    // Строка часов: контейнер по ширине содержимого, выровнен по центру
    lv_obj_t* row = lv_obj_create(s);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 2, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(row, LV_ALIGN_TOP_MID, 0, 38);

    clockH.obj = makeLabel(row, &lv_font_clock_70, C_TEXT, "12");
    clockColon = makeLabel(row, &lv_font_clock_70, C_ACCENT, ":");
    clockM.obj = makeLabel(row, &lv_font_clock_70, C_TEXT, "34");
    colonShown = -1;

    makeTile(clkBoost, s, 8, 108, 96, 114, "BOOST", "", 34, false, -100, 150);
    makeTile(clkClt,   s, 112, 108, 96, 114, "WATER", "\xC2\xB0" "C", 34, false, 40, 120);
    makeTile(clkAfr,   s, 216, 108, 96, 114, "AFR", "", 34, false, 100, 200);
    // На главном экране значения крупнее
    lv_obj_set_style_text_font(clkBoost.value.obj, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_font(clkClt.value.obj, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_font(clkAfr.value.obj, &lv_font_montserrat_32, 0);
}

// -----------------------------------------------------------------------------
// ЭКРАН 2: ТРИП И РАСХОД
// -----------------------------------------------------------------------------
void UiEngine::createScreenTrip() {
    lv_obj_t* s = makeScreen();
    screens[(int)ScreenId::OBC_TRIP_FUEL] = s;

    lv_obj_t* top = makeBox(s, 8, 28, 304, 82, C_SURFACE, 10);

    tripSpeed.obj = makeLabel(top, &lv_font_clock_70, C_TEXT, "0");
    lv_obj_set_pos(tripSpeed.obj, 12, 8);
    lv_obj_t* kmh = makeCaption(top, "KM/H");
    lv_obj_set_pos(kmh, 14, 62);
    tripFix.obj = makeLabel(top, &lv_font_montserrat_12, C_DIM, "");
    lv_obj_set_pos(tripFix.obj, 60, 62);

    lv_obj_t* div = makeBox(top, 158, 12, 1, 58, C_SURFACE2, 0);
    (void)div;

    lv_obj_t* instCap = makeCaption(top, "INSTANT");
    lv_obj_set_pos(instCap, 172, 8);
    tripInstUnit.obj = makeLabel(top, &lv_font_montserrat_12, C_DIM, "L/H");
    lv_obj_align(tripInstUnit.obj, LV_ALIGN_TOP_RIGHT, -10, 8);
    tripInst.obj = makeLabel(top, &lv_font_telemetry_24, C_TEXT, "0.0");
    lv_obj_set_pos(tripInst.obj, 172, 28);
    tripInstBar = makeBar(top, 172, 66, 122, 5, 0, 300);

    makeTile(tripDist, s, 8, 118, 96, 104, "TRIP", "KM", 36, true, 0, 0);
    makeTile(tripAvg,  s, 112, 118, 96, 104, "AVG", "L/100", 36, true, 0, 0);
    makeTile(tripFuel, s, 216, 118, 96, 104, "FUEL", "L", 36, true, 0, 0);
    tripDist.sub.set("HOLD: RESET");
    tripAvg.sub.set("AVERAGE");
    tripFuel.sub.set("USED");
}

void UiEngine::notifyTripReset() {
    tripDist.sub.set("RESET OK");
    if (tripDist.sub.obj) lv_obj_set_style_text_color(tripDist.sub.obj, C_OK, 0);
    tripResetNoticeMs = millis();
}

// -----------------------------------------------------------------------------
// ЭКРАН 3: ТЕЛЕМЕТРИЯ МОТОРА
// -----------------------------------------------------------------------------
void UiEngine::createScreenTelem() {
    lv_obj_t* s = makeScreen();
    screens[(int)ScreenId::OBC_TELEMETRY] = s;

    makeTile(engVolt, s, 8, 28, 148, 74, "BATTERY", "V", 24, true, 0, 0);
    makeTile(engClt,  s, 164, 28, 148, 74, "WATER", "\xC2\xB0" "C", 24, true, 0, 0);
    makeTile(engAfr,  s, 8, 108, 148, 74, "AFR", "", 24, true, 0, 0);
    makeTile(engTps,  s, 164, 108, 148, 74, "THROTTLE", "%", 24, true, 0, 0);

    // Наддув: подпись и значение в строку, шкала на всю ширину с меткой нуля
    engBoost.box = makeBox(s, 8, 188, 304, 36, C_SURFACE, 10);
    lv_obj_t* cap = makeCaption(engBoost.box, "BOOST");
    lv_obj_set_pos(cap, 10, 4);
    engBoost.value.obj = makeLabel(engBoost.box, &lv_font_montserrat_12, C_TEXT, "--");
    lv_obj_align(engBoost.value.obj, LV_ALIGN_TOP_RIGHT, -10, 4);
    engBoost.bar = makeBar(engBoost.box, 10, 24, 284, 6, -100, 150);
    // Метка 0 бар (атмосфера) на шкале -1.0..+1.5
    makeBox(engBoost.box, 10 + (284 * 100) / 250, 21, 2, 12, C_DIM, 0);
}

// -----------------------------------------------------------------------------
// ЭКРАН 4: ///M ТАХОМЕТР
// -----------------------------------------------------------------------------
void UiEngine::createScreenMPerf() {
    lv_obj_t* s = makeScreen();
    screens[(int)ScreenId::M_PERFORMANCE] = s;

    // Шифт-лайт: 16 сегментов (синие -> янтарные -> красные)
    const int segW = 17, segGap = 2;
    int x0 = (320 - (SHIFT_SEGMENTS * segW + (SHIFT_SEGMENTS - 1) * segGap)) / 2;
    for (int i = 0; i < SHIFT_SEGMENTS; i++) {
        shiftSeg[i] = makeBox(s, x0 + i * (segW + segGap), 30, segW, 12, C_SURFACE2, 3);
    }

    mRpm.obj = makeLabel(s, &lv_font_clock_96, C_TEXT, "0");
    lv_obj_align(mRpm.obj, LV_ALIGN_TOP_RIGHT, -70, 52);
    lv_obj_t* rpmCap = makeCaption(s, "RPM");
    lv_obj_align(rpmCap, LV_ALIGN_TOP_RIGHT, -20, 106);
    lv_obj_t* stripe = makeBox(s, 262, 96, 38, 4, C_M_BLUE, 2);
    (void)stripe;

    makeTile(mBoost, s, 8, 150, 148, 72, "BOOST", "BAR", 26, false, -100, 150);
    makeTile(mAdv,   s, 164, 150, 148, 72, "IGNITION", "\xC2\xB0" "BTDC", 26, false, 0, 45);
}

// -----------------------------------------------------------------------------
// ЭКРАН 5: ПОЛНОЭКРАННЫЙ ПРИБОР (дуга 270°)
// -----------------------------------------------------------------------------
void UiEngine::createScreenGauge() {
    lv_obj_t* s = makeScreen();
    screens[(int)ScreenId::SINGLE_GAUGE] = s;

    gaugeArc = lv_arc_create(s);
    lv_obj_set_size(gaugeArc, 188, 188);
    lv_obj_align(gaugeArc, LV_ALIGN_TOP_MID, 0, 30);
    lv_arc_set_rotation(gaugeArc, 135);
    lv_arc_set_bg_angles(gaugeArc, 0, 270);
    lv_arc_set_range(gaugeArc, 0, 1000);
    lv_arc_set_value(gaugeArc, 0);
    lv_obj_remove_style(gaugeArc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(gaugeArc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_pad_all(gaugeArc, 0, 0);
    lv_obj_set_style_arc_width(gaugeArc, 12, LV_PART_MAIN);
    lv_obj_set_style_arc_color(gaugeArc, C_SURFACE2, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(gaugeArc, true, LV_PART_MAIN);
    lv_obj_set_style_arc_width(gaugeArc, 12, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(gaugeArc, C_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(gaugeArc, true, LV_PART_INDICATOR);

    gaugeVal.obj = makeLabel(s, &lv_font_clock_70, C_TEXT, "0");
    lv_obj_align(gaugeVal.obj, LV_ALIGN_TOP_MID, 0, 96);
    gaugeUnit.obj = makeLabel(s, &lv_font_montserrat_14, C_DIM, "");
    lv_obj_set_style_text_letter_space(gaugeUnit.obj, 1, 0);
    lv_obj_align(gaugeUnit.obj, LV_ALIGN_TOP_MID, 0, 152);

    gaugeMin.obj = makeLabel(s, &lv_font_montserrat_12, C_DIM, "");
    lv_obj_align(gaugeMin.obj, LV_ALIGN_TOP_MID, -52, 196);
    gaugeMax.obj = makeLabel(s, &lv_font_montserrat_12, C_DIM, "");
    lv_obj_align(gaugeMax.obj, LV_ALIGN_TOP_MID, 52, 196);

    gaugePeak.obj = makeLabel(s, &lv_font_montserrat_12, C_ACCENT, "");
    lv_obj_set_pos(gaugePeak.obj, 10, 208);
    gaugeLimit.obj = makeLabel(s, &lv_font_montserrat_12, C_DIM, "");
    lv_obj_align(gaugeLimit.obj, LV_ALIGN_TOP_RIGHT, -10, 208);
}

// -----------------------------------------------------------------------------
// ЭКРАН 6: СИСТЕМА
// -----------------------------------------------------------------------------
static void makeInfoRow(CachedLabel& value, lv_obj_t* parent, int y, const char* caption) {
    lv_obj_t* row = makeBox(parent, 8, y, 304, 44, C_SURFACE, 10);
    lv_obj_t* cap = makeCaption(row, caption);
    lv_obj_align(cap, LV_ALIGN_LEFT_MID, 12, 0);
    value.obj = makeLabel(row, &lv_font_montserrat_14, C_TEXT, "");
    lv_obj_align(value.obj, LV_ALIGN_RIGHT_MID, -12, 0);
}

void UiEngine::createScreenSettings() {
    lv_obj_t* s = makeScreen();
    screens[(int)ScreenId::SETTINGS_INFO] = s;

    makeInfoRow(infoCan, s, 28, "CAN MS2");
    makeInfoRow(infoGps, s, 78, "GPS");
    makeInfoRow(infoBle, s, 128, "BLE OTA");
    makeInfoRow(infoFw,  s, 178, "FIRMWARE");
}

// =============================================================================
// ЗАСТАВКА И НАВИГАЦИЯ
// =============================================================================
void UiEngine::showBootSplash(const char* subtitle) {
    Display.setBrightness(DEFAULT_BRIGHTNESS);
    Adafruit_ST7789& tft = Display.getTft();
    tft.fillScreen(0x0000);

    tft.setFont(&FreeSansBoldOblique12pt7b);
    tft.setTextColor(0xFFFF);
    tft.setCursor(68, 105);
    tft.print("Megasquirt 2");
    tft.drawFastHLine(48, 120, 224, 0xFC43); // янтарная линия
    tft.setFont();

    if (subtitle && subtitle[0]) {
        tft.setTextColor(0x8430);
        tft.setTextSize(1);
        int16_t x = 160 - (strlen(subtitle) * 6) / 2;
        if (x < 10) x = 10;
        tft.setCursor(x, 132);
        tft.print(subtitle);
    }

    delay(900);
}

void UiEngine::nextScreen() {
    int next = ((int)currentScreen + 1);
    if (next >= (int)ScreenId::COUNT) {
        next = (int)ScreenId::CLASSIC_CLOCK;
    }
    Serial.printf("[UI] Переключение экрана: %d -> %d\n", (int)currentScreen, next);
    setScreen((ScreenId)next);
}

void UiEngine::setScreen(ScreenId screen) {
    if ((int)screen <= 0 || screen >= ScreenId::COUNT || screen == currentScreen) return;

    // Направление сдвига: вперед - влево, назад (в т.ч. с последнего на первый) - вправо
    bool forward = (int)screen > (int)currentScreen;
    currentScreen = screen;

    // Свежие данные до начала анимации, чтобы не было скачка значений в конце сдвига
    switch (screen) {
        case ScreenId::CLASSIC_CLOCK:  updateClockScreen(); break;
        case ScreenId::OBC_TRIP_FUEL:  updateTripScreen(); break;
        case ScreenId::OBC_TELEMETRY:  updateTelemScreen(); break;
        case ScreenId::M_PERFORMANCE:  updateMPerfScreen(); break;
        case ScreenId::SINGLE_GAUGE:   updateGaugeScreen(true); break;
        case ScreenId::SETTINGS_INFO:  updateSettingsScreen(); break;
        default: break;
    }
    updateTopLayer();

    lv_scr_load_anim(screens[(int)screen],
                     forward ? LV_SCR_LOAD_ANIM_MOVE_LEFT : LV_SCR_LOAD_ANIM_MOVE_RIGHT,
                     SCREEN_ANIM_MS, 0, false);
}

// =============================================================================
// ГЛАВНЫЙ ЦИКЛ UI
// =============================================================================
void UiEngine::update() {
    unsigned long now = millis();

    if (now - lastDataMs >= DATA_PERIOD_MS) {
        float dt = (float)(now - lastDataMs) / 1000.0f;
        if (dt > 0.5f) dt = 0.5f;
        lastDataMs = now;

        stepSmoothing(dt);
        updateTopLayer();

        switch (currentScreen) {
            case ScreenId::CLASSIC_CLOCK:  updateClockScreen(); break;
            case ScreenId::OBC_TRIP_FUEL:  updateTripScreen(); break;
            case ScreenId::OBC_TELEMETRY:  updateTelemScreen(); break;
            case ScreenId::M_PERFORMANCE:  updateMPerfScreen(); break;
            case ScreenId::SINGLE_GAUGE:   updateGaugeScreen(); break;
            case ScreenId::SETTINGS_INFO:  updateSettingsScreen(); break;
            default: break;
        }
    }

    // Отрисовка: LVGL сам ограничивает частоту кадров (LV_DISP_DEF_REFR_PERIOD)
    lv_timer_handler();
}

// Сглаживание: быстрые параметры (обороты, наддув) - короткая постоянная времени,
// медленные (температуры, напряжение) - длинная, чтобы цифры не "дрожали"
void UiEngine::stepSmoothing(float dt) {
    const SensorData& s = Sensors.getData();
    const TripData& tr = Trip.getData();
    bool on = s.ms2Online;

    sRpm.step(on ? (float)s.ms2.rpm : 0.0f, dt, 0.08f);
    sBoost.step(on ? s.ms2.boost_bar : 0.0f, dt, 0.10f);
    sAfr.step(on ? s.ms2.afr : 14.7f, dt, 0.15f);
    sTps.step(on ? s.ms2.tps_pct : 0.0f, dt, 0.08f);
    sAdv.step(on ? s.ms2.advance_deg : 0.0f, dt, 0.15f);
    sClt.step(on ? s.ms2.clt_c : 0.0f, dt, 0.8f);
    sMat.step(on ? s.ms2.mat_c : 0.0f, dt, 0.8f);
    sVolt.step(s.batteryVoltage, dt, 0.5f);
    sSpeed.step(tr.current_speed_kmh, dt, 0.25f);
    sInst.step(tr.instant_consumption, dt, 0.4f);

    // При появлении/пропадании связи - без "наезда" значений из нуля
    static bool lastOn = false;
    if (on != lastOn) {
        lastOn = on;
        if (on) {
            sRpm.snap(s.ms2.rpm); sBoost.snap(s.ms2.boost_bar); sAfr.snap(s.ms2.afr);
            sClt.snap(s.ms2.clt_c); sMat.snap(s.ms2.mat_c); sTps.snap(s.ms2.tps_pct);
            sAdv.snap(s.ms2.advance_deg);
        }
    }
}

void UiEngine::updateTopLayer() {
    const SensorData& sens = Sensors.getData();
    const GpsData& gps = Gps.getData();

    static int lastMs2 = -1;
    int ms2 = sens.ms2Online ? 1 : 0;
    if (ms2 != lastMs2) {
        lastMs2 = ms2;
        lv_obj_set_style_bg_color(ms2Dot, ms2 ? C_OK : C_SURFACE2, 0);
        lv_obj_set_style_text_color(ms2Text.obj, ms2 ? C_TEXT : C_DIM, 0);
    }
    ms2Text.set(CanBus.isDemo() ? "MS2 DEMO" : "MS2");

    static int lastFix = -1;
    if ((gps.hasFix ? 1 : 0) != lastFix) {
        lastFix = gps.hasFix ? 1 : 0;
        lv_obj_set_style_text_color(gpsLbl.obj, gps.hasFix ? C_TEXT : C_DIM, 0);
    }
    if (gps.satellites > 0) {
        gpsLbl.setf(LV_SYMBOL_GPS " %d", gps.satellites);
    } else {
        gpsLbl.set(LV_SYMBOL_GPS " --");
    }
    lv_obj_align(gpsLbl.obj, LV_ALIGN_TOP_RIGHT, -12, 5);

    // Заголовок экрана
    static const char* gaugeNames[] = { "BOOST", "WATER", "AFR", "FUEL", "SPEED", "INTAKE", "TACHO" };
    const char* title = "";
    char buf[32];
    switch (currentScreen) {
        case ScreenId::CLASSIC_CLOCK:  title = Time.getTime().dayOfWeekStrEn; break;
        case ScreenId::OBC_TRIP_FUEL:  title = "TRIP"; break;
        case ScreenId::OBC_TELEMETRY:  title = "ENGINE"; break;
        case ScreenId::M_PERFORMANCE:  title = "///M"; break;
        case ScreenId::SINGLE_GAUGE:
            snprintf(buf, sizeof(buf), "%s  %d/7", gaugeNames[(int)currentGauge], (int)currentGauge + 1);
            title = buf;
            break;
        case ScreenId::SETTINGS_INFO:  title = "SYSTEM"; break;
        default: break;
    }
    titleLbl.set(title);
    lv_obj_align(titleLbl.obj, LV_ALIGN_TOP_MID, 0, 5);

    // Индикатор текущего экрана: активная точка вытягивается в янтарную "таблетку"
    static int lastPage = -1;
    int page = (int)currentScreen - 1;
    if (page != lastPage) {
        const int n = (int)ScreenId::COUNT - 1;
        const int gap = 10;
        int x0 = 160 - ((n - 1) * gap) / 2 - 2;
        for (int i = 0; i < n; i++) {
            bool act = (i == page);
            lv_obj_set_size(pageDots[i], act ? 12 : 4, 4);
            lv_obj_set_x(pageDots[i], x0 + i * gap - (act ? 4 : 0));
            lv_obj_set_style_bg_color(pageDots[i], act ? C_ACCENT : C_SURFACE2, 0);
        }
        lastPage = page;
    }
}

// -----------------------------------------------------------------------------
void UiEngine::updateClockScreen() {
    const TimeData& td = Time.getTime();
    const SensorData& sens = Sensors.getData();
    const WarningState& warn = Warnings.getState();
    bool on = sens.ms2Online;

    // Двоеточие мигает раз в секунду: меняется только прозрачность, ширина строки постоянна
    clockH.setf("%02d", td.hour);
    clockM.setf("%02d", td.minute);
    int8_t colon = (millis() % 1000) < 500 ? 1 : 0;
    if (colon != colonShown) {
        colonShown = colon;
        lv_obj_set_style_text_opa(clockColon, colon ? LV_OPA_COVER : LV_OPA_20, 0);
    }

    clkBoost.setAlarm(warn.boostAlarm, warn.blinkPhase);
    clkClt.setAlarm(warn.cltAlarm, warn.blinkPhase);
    clkAfr.setAlarm(warn.afrAlarm, warn.blinkPhase);

    if (on) {
        // Без знака "+": крупный шрифт должен уместиться в плитку (минус остается)
        clkBoost.value.setf("%.2f", sBoost.value);
        clkClt.value.setf("%d", (int)lroundf(sClt.value));
        clkAfr.value.setf("%.1f", sAfr.value);
        clkBoost.setBar(clampI(lroundf(sBoost.value * 100), -100, 150));
        clkClt.setBar(clampI(lroundf(sClt.value), 40, 120));
        clkAfr.setBar(clampI(lroundf(sAfr.value * 10), 100, 200));
    } else {
        clkBoost.value.set("-.--");
        clkClt.value.set("--");
        clkAfr.value.set("--.-");
        clkBoost.setBar(-100);
        clkClt.setBar(40);
        clkAfr.setBar(100);
    }
}

void UiEngine::updateTripScreen() {
    const TripData& trip = Trip.getData();
    const GpsData& gps = Gps.getData();

    tripSpeed.setf("%d", (int)clampI(lroundf(sSpeed.value), 0, 999));
    tripFix.set(gps.hasFix ? "" : "NO GPS FIX");

    tripInst.setf("%.1f", sInst.value);
    tripInstUnit.set(trip.isLitersPerHour ? "L/H" : "L/100");
    lv_obj_align(tripInstUnit.obj, LV_ALIGN_TOP_RIGHT, -10, 8);
    int32_t ib = clampI(lroundf(sInst.value * 10), 0, 300);
    if (ib != tripInstBarLast) {
        tripInstBarLast = ib;
        lv_bar_set_value(tripInstBar, ib, LV_ANIM_OFF);
    }

    if (trip.trip_distance_km >= 1000.0) {
        tripDist.value.setf("%.0f", trip.trip_distance_km);
    } else {
        tripDist.value.setf("%.1f", trip.trip_distance_km);
    }
    if (trip.trip_distance_km >= 0.1) {
        tripAvg.value.setf("%.1f", trip.avg_consumption_l_100km);
    } else {
        tripAvg.value.set("--.-");
    }
    tripFuel.value.setf("%.1f", trip.trip_fuel_liters);

    if (tripResetNoticeMs > 0 && (millis() - tripResetNoticeMs >= 2500)) {
        tripResetNoticeMs = 0;
        tripDist.sub.set("HOLD: RESET");
        lv_obj_set_style_text_color(tripDist.sub.obj, C_DIM, 0);
    }
}

void UiEngine::updateTelemScreen() {
    const SensorData& sens = Sensors.getData();
    const WarningState& warn = Warnings.getState();
    bool on = sens.ms2Online;

    engClt.setAlarm(warn.cltAlarm, warn.blinkPhase);
    engAfr.setAlarm(warn.afrAlarm, warn.blinkPhase);
    engBoost.setAlarm(warn.boostAlarm, warn.blinkPhase);

    // АКБ
    if (sens.batteryVoltage < 1.0f) {
        engVolt.value.set("--.-");
        engVolt.sub.set("NO DATA");
    } else {
        engVolt.value.setf("%.1f", sVolt.value);
        if (sens.voltStatus == VoltageStatus::VOLT_CRITICAL_LOW) engVolt.sub.set("LOW BATTERY");
        else if (sens.voltStatus == VoltageStatus::VOLT_NORMAL_RUNNING) engVolt.sub.set("CHARGING");
        else if (sens.voltStatus == VoltageStatus::VOLT_OVERCHARGE) engVolt.sub.set("OVERCHARGE");
        else engVolt.sub.setf("CRANK MIN %.1f", sens.minCrankVoltage);
    }

    if (on) {
        engClt.value.setf("%d", (int)lroundf(sClt.value));
        engClt.sub.setf("INTAKE %d\xC2\xB0" "C", (int)lroundf(sMat.value));
        engAfr.value.setf("%.1f", sAfr.value);
        engAfr.sub.setf("TARGET %.1f", sens.ms2.afr_target);
        engTps.value.setf("%d", (int)clampI(lroundf(sTps.value), 0, 100));
        engTps.sub.setf("MAP %d KPA", (int)lroundf(sens.ms2.map_kpa));
        engBoost.value.setf("%+.2f BAR", sBoost.value);
        engBoost.setBar(clampI(lroundf(sBoost.value * 100), -100, 150));
    } else {
        engClt.value.set("--");
        engClt.sub.set("NO CAN DATA");
        engAfr.value.set("--.-");
        engAfr.sub.set("NO CAN DATA");
        engTps.value.set("--");
        engTps.sub.set("NO CAN DATA");
        engBoost.value.set("--");
        engBoost.setBar(-100);
    }
    lv_obj_align(engBoost.value.obj, LV_ALIGN_TOP_RIGHT, -10, 4);
}

void UiEngine::updateMPerfScreen() {
    const SensorData& sens = Sensors.getData();
    const WarningState& warn = Warnings.getState();
    bool on = sens.ms2Online;

    mBoost.setAlarm(warn.boostAlarm, warn.blinkPhase);

    int rpm = on ? (int)lroundf(sRpm.value) : 0;
    // Цифры оборотов округляем до 10, чтобы младший разряд не мельтешил
    mRpm.setf("%d", on ? (rpm / 10) * 10 : 0);
    lv_obj_align(mRpm.obj, LV_ALIGN_TOP_RIGHT, -70, 52);

    // Шифт-лайт: сегменты загораются от 1000 до 6600 об/мин, на отсечке мигают все красным
    const int startRpm = 1000, fullRpm = 6600;
    int lit = (rpm <= startRpm) ? 0 : ((rpm - startRpm) * SHIFT_SEGMENTS) / (fullRpm - startRpm) + 1;
    lit = clampI(lit, 0, SHIFT_SEGMENTS);
    bool flash = on && rpm >= fullRpm && ((millis() / 80) % 2 == 0);
    if (lit != shiftLit || flash != shiftFlash) {
        shiftLit = lit;
        shiftFlash = flash;
        for (int i = 0; i < SHIFT_SEGMENTS; i++) {
            lv_color_t c;
            if (rpm >= fullRpm) {
                c = flash ? C_M_RED : C_SURFACE2;
            } else if (i < lit) {
                c = (i < 9) ? C_M_LBLUE : (i < 13 ? C_ACCENT : C_M_RED);
            } else {
                c = C_SURFACE2;
            }
            lv_obj_set_style_bg_color(shiftSeg[i], c, 0);
        }
    }

    if (on) {
        mBoost.value.setf("%+.2f", sBoost.value);
        mBoost.setBar(clampI(lroundf(sBoost.value * 100), -100, 150));
        mAdv.value.setf("%.1f", sAdv.value);
        mAdv.setBar(clampI(lroundf(sAdv.value), 0, 45));
    } else {
        mBoost.value.set("-.--");
        mBoost.setBar(-100);
        mAdv.value.set("--.-");
        mAdv.setBar(0);
    }
}

void UiEngine::updateGaugeScreen(bool force) {
    const SensorData& sens = Sensors.getData();
    const TripData& trip = Trip.getData();
    const TripPeaks& peaks = Trip.getPeaks();
    const WarningState& warn = Warnings.getState();
    const WarningSettings& ws = Warnings.getSettings();
    bool on = sens.ms2Online;

    static GaugeType lastG = GaugeType::COUNT;
    bool changed = force || (currentGauge != lastG);
    lastG = currentGauge;

    float v = 0.0f, vMin = 0.0f, vMax = 1.0f;
    bool valid = true;
    bool isAlarm = false;
    char val[16], peak[32], limit[32];
    const char* unit = "";
    const char* minTxt = "";
    const char* maxTxt = "";

    switch (currentGauge) {
        case GaugeType::BOOST:
            v = sBoost.value; vMin = -1.0f; vMax = 1.5f; valid = on;
            snprintf(val, sizeof(val), "%+.2f", v);
            snprintf(peak, sizeof(peak), "PEAK %+.2f", peaks.peak_boost_bar);
            snprintf(limit, sizeof(limit), "LIMIT %.2f", ws.boost_max_bar);
            unit = "BAR"; minTxt = "-1.0"; maxTxt = "+1.5";
            isAlarm = warn.boostAlarm;
            break;
        case GaugeType::COOLANT:
            v = sClt.value; vMin = 40.0f; vMax = 130.0f; valid = on;
            snprintf(val, sizeof(val), "%d", (int)lroundf(v));
            snprintf(peak, sizeof(peak), "PEAK %d\xC2\xB0", (int)lroundf(peaks.max_clt_c));
            snprintf(limit, sizeof(limit), "LIMIT %d\xC2\xB0", (int)lroundf(ws.clt_max_c));
            unit = "\xC2\xB0" "C"; minTxt = "40"; maxTxt = "130";
            isAlarm = warn.cltAlarm;
            break;
        case GaugeType::AFR:
            v = sAfr.value; vMin = 10.0f; vMax = 20.0f; valid = on;
            snprintf(val, sizeof(val), "%.1f", v);
            if (peaks.min_afr <= 30.0f && peaks.max_afr >= 5.0f) {
                snprintf(peak, sizeof(peak), "RANGE %.1f-%.1f", peaks.min_afr, peaks.max_afr);
            } else {
                snprintf(peak, sizeof(peak), "RANGE --");
            }
            snprintf(limit, sizeof(limit), "SAFE %.1f-%.1f", ws.afr_rich_min, ws.afr_lean_max);
            unit = "AFR"; minTxt = "10"; maxTxt = "20";
            isAlarm = warn.afrAlarm;
            break;
        case GaugeType::INST_FUEL:
            v = sInst.value; vMin = 0.0f; vMax = 30.0f;
            snprintf(val, sizeof(val), "%.1f", v);
            snprintf(peak, sizeof(peak), "PEAK %.1f", peaks.peak_instant_fuel);
            snprintf(limit, sizeof(limit), "AVG %.1f", trip.avg_consumption_l_100km);
            unit = trip.isLitersPerHour ? "L / HOUR" : "L / 100 KM"; minTxt = "0"; maxTxt = "30";
            break;
        case GaugeType::SPEED:
            v = sSpeed.value; vMin = 0.0f; vMax = 240.0f;
            snprintf(val, sizeof(val), "%d", (int)lroundf(v));
            snprintf(peak, sizeof(peak), "MAX %d", (int)lroundf(peaks.max_speed_kmh));
            snprintf(limit, sizeof(limit), "AVG %d", (int)lroundf(trip.avg_speed_kmh));
            unit = "KM / H"; minTxt = "0"; maxTxt = "240";
            break;
        case GaugeType::INTAKE_TEMP:
            v = sMat.value; vMin = 0.0f; vMax = 80.0f; valid = on;
            snprintf(val, sizeof(val), "%d", (int)lroundf(v));
            snprintf(peak, sizeof(peak), "PEAK %d\xC2\xB0", (int)lroundf(peaks.max_iat_c));
            snprintf(limit, sizeof(limit), "LIMIT 55\xC2\xB0");
            unit = "\xC2\xB0" "C"; minTxt = "0"; maxTxt = "80";
            isAlarm = on && sMat.value >= 55.0f;
            break;
        case GaugeType::RPM:
        default:
            v = sRpm.value; vMin = 0.0f; vMax = 7500.0f; valid = on;
            snprintf(val, sizeof(val), "%d", ((int)lroundf(v) / 10) * 10);
            snprintf(peak, sizeof(peak), "PEAK %d", (int)peaks.max_rpm);
            snprintf(limit, sizeof(limit), "REDLINE 6500");
            unit = "RPM"; minTxt = "0"; maxTxt = "7500";
            isAlarm = on && v >= 6500.0f;
            break;
    }

    if (!valid) {
        snprintf(val, sizeof(val), "--");
        v = vMin;
    }

    gaugeVal.set(val);
    lv_obj_align(gaugeVal.obj, LV_ALIGN_TOP_MID, 0, 96);
    gaugePeak.set(peak);
    gaugeLimit.set(limit);
    lv_obj_align(gaugeLimit.obj, LV_ALIGN_TOP_RIGHT, -10, 208);
    if (changed) {
        gaugeUnit.set(unit);
        lv_obj_align(gaugeUnit.obj, LV_ALIGN_TOP_MID, 0, 152);
        gaugeMin.set(minTxt);
        gaugeMax.set(maxTxt);
        lv_obj_align(gaugeMin.obj, LV_ALIGN_TOP_MID, -52, 196);
        lv_obj_align(gaugeMax.obj, LV_ALIGN_TOP_MID, 52, 196);
    } else {
        gaugeUnit.set(unit); // L/H <-> L/100 меняется на ходу
    }

    int32_t arcVal = clampI(lroundf((v - vMin) / (vMax - vMin) * 1000.0f), 0, 1000);
    if (arcVal != gaugeArcLast) {
        gaugeArcLast = arcVal;
        lv_arc_set_value(gaugeArc, arcVal);
    }

    uintptr_t alarmState = isAlarm ? (warn.blinkPhase ? 2 : 1) : 0;
    if (changed || alarmState != gaugeAlarmState) {
        gaugeAlarmState = alarmState;
        lv_obj_set_style_arc_color(gaugeArc, isAlarm ? C_M_RED : C_ACCENT, LV_PART_INDICATOR);
        lv_obj_set_style_text_color(gaugeVal.obj, alarmState == 2 ? C_M_RED : C_TEXT, 0);
    }
}

void UiEngine::updateSettingsScreen() {
    const SensorData& sens = Sensors.getData();
    const GpsData& gps = Gps.getData();

    if (CanBus.isDemo()) {
        infoCan.set("DEMO MODE");
    } else {
        infoCan.setf("%s  %lu", sens.ms2Online ? "ONLINE" : "OFFLINE",
                     (unsigned long)CanBus.getTelemetry().packetsTotal);
    }
    infoGps.setf("%s  %d SAT", gps.hasFix ? "FIX" : "SEARCH", gps.satellites);
    if (Ota.isActive()) infoBle.set("UPDATING...");
    else infoBle.set(BleMgr.isConnected() ? "CONNECTED" : "READY");
    unsigned long up = millis() / 1000UL;
    infoFw.setf("v%s  %luh%02lum", FW_VERSION, up / 3600UL, (up / 60UL) % 60UL);

    lv_obj_align(infoCan.obj, LV_ALIGN_RIGHT_MID, -12, 0);
    lv_obj_align(infoGps.obj, LV_ALIGN_RIGHT_MID, -12, 0);
    lv_obj_align(infoBle.obj, LV_ALIGN_RIGHT_MID, -12, 0);
    lv_obj_align(infoFw.obj, LV_ALIGN_RIGHT_MID, -12, 0);
}

// =============================================================================
// ВЫБОР ПРИБОРА
// =============================================================================
void UiEngine::setGaugeType(GaugeType type) {
    if (type >= GaugeType::COUNT) type = GaugeType::BOOST;
    currentGauge = type;
    saveGaugeToNvs();
    updateTopLayer();
    updateGaugeScreen(true);
}

void UiEngine::nextGaugeType() {
    int next = ((int)currentGauge + 1);
    if (next >= (int)GaugeType::COUNT) next = 0;
    setGaugeType((GaugeType)next);
    Serial.printf("[UI] Прибор: %d\n", next);
}

void UiEngine::loadGaugeFromNvs() {
    Preferences prefs;
    if (prefs.begin("obc_ui", true)) {
        uint8_t g = prefs.getUChar("gauge", 0);
        if (g < (uint8_t)GaugeType::COUNT) {
            currentGauge = (GaugeType)g;
        }
        prefs.end();
    }
}

void UiEngine::saveGaugeToNvs() {
    Preferences prefs;
    if (prefs.begin("obc_ui", false)) {
        prefs.putUChar("gauge", (uint8_t)currentGauge);
        prefs.end();
    }
}
