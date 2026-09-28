#include "ui_engine.h"
#include "ble_manager.h"
#include <Preferences.h>
#include <Fonts/FreeSansBoldOblique12pt7b.h>

UiEngine UI;

// Цвета BMW Motorsport / OEM+ Cockpit
#define COLOR_LV_WHITE        lv_color_make(255, 255, 255)
#define COLOR_LV_BLACK        lv_color_make(0, 0, 0)
#define COLOR_LV_SILVER       lv_color_make(170, 175, 185)
#define COLOR_LV_CARD_BG      lv_color_make(18, 20, 25)
#define COLOR_LV_CARD_BORDER  lv_color_make(48, 52, 64)
#define COLOR_LV_DARK_GRAY    lv_color_make(40, 42, 50)
#define COLOR_LV_MID_GRAY     lv_color_make(115, 120, 130)
#define COLOR_LV_M_RED        lv_color_make(225, 30, 40)
#define COLOR_LV_M_BLUE       lv_color_make(0, 102, 177)
#define COLOR_LV_M_CYAN       lv_color_make(0, 176, 255)
#define COLOR_LV_ONLINE       lv_color_make(0, 230, 118)
#define COLOR_LV_OFFLINE      lv_color_make(115, 120, 130)

UiEngine::UiEngine()
    : currentScreen(ScreenId::CLASSIC_CLOCK),
      lastScreen(ScreenId::BOOT_SPLASH),
      colonState(true),
      lastColonBlinkMillis(0),
      lastTickMillis(0),
      scr_clock(nullptr),
      scr_trip(nullptr),
      scr_telem(nullptr),
      scr_m_perf(nullptr),
      scr_gauge(nullptr),
      scr_settings(nullptr),
      lbl_clock(nullptr),
      lbl_gps_sats_1(nullptr),
      lbl_ms2_status_1(nullptr),
      card_boost_1(nullptr),
      lbl_boost_val(nullptr),
      lbl_boost_sub(nullptr),
      card_clt_1(nullptr),
      lbl_clt_val(nullptr),
      lbl_clt_sub(nullptr),
      card_afr_1(nullptr),
      lbl_afr_val(nullptr),
      lbl_afr_sub(nullptr),
      lbl_trip_spd(nullptr),
      lbl_trip_fix(nullptr),
      lbl_trip_inst(nullptr),
      card_trip_dist(nullptr),
      lbl_trip_dist(nullptr),
      lbl_trip_reset_hint(nullptr),
      card_trip_avg(nullptr),
      lbl_trip_avg(nullptr),
      card_trip_fuel(nullptr),
      lbl_trip_fuel(nullptr),
      lbl_gps_sats_2(nullptr),
      lbl_ms2_status_2(nullptr),
      lbl_telem_volt(nullptr),
      lbl_telem_volt_sub(nullptr),
      card_telem_clt(nullptr),
      lbl_telem_clt(nullptr),
      lbl_telem_clt_sub(nullptr),
      card_telem_afr(nullptr),
      lbl_telem_afr(nullptr),
      lbl_telem_afr_sub(nullptr),
      lbl_telem_tps(nullptr),
      lbl_telem_tps_sub(nullptr),
      card_telem_boost(nullptr),
      lbl_telem_boost_txt(nullptr),
      bar_telem_boost(nullptr),
      lbl_gps_sats_3(nullptr),
      lbl_ms2_status_3(nullptr),
      lbl_m_rpm(nullptr),
      bar_m_shift(nullptr),
      card_m_boost(nullptr),
      lbl_m_boost(nullptr),
      lbl_m_adv(nullptr),
      lbl_m_offline(nullptr),
      lbl_gps_sats_4(nullptr),
      lbl_ms2_status_4(nullptr),
      currentGauge(GaugeType::BOOST),
      card_digital_gauge(nullptr),
      lbl_digital_title(nullptr),
      lbl_digital_val(nullptr),
      lbl_digital_unit(nullptr),
      lbl_digital_peak(nullptr),
      lbl_digital_warn_limit(nullptr),
      lbl_set_ble(nullptr),
      lbl_set_can(nullptr),
      lbl_set_gps(nullptr),
      lbl_set_sys(nullptr) {
}

// -----------------------------------------------------------------------------
// Callback сброса буфера кадра LVGL на дисплей ST7789 через SPI 40 МГц
// -----------------------------------------------------------------------------
void UiEngine::dispFlushCb(lv_disp_drv_t* disp_drv, const lv_area_t* area, lv_color_t* color_p) {
    static uint32_t flushCount = 0;
    flushCount++;
    if (flushCount <= 3 || flushCount % 100 == 0) {
        Serial.printf("[LVGL] flush #%lu: (%d,%d)-(%d,%d)\n", flushCount, area->x1, area->y1, area->x2, area->y2);
    }

    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);

    Adafruit_ST7789& tft = Display.getTft();
    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    // bigEndian = true, так как в lv_conf.h активирован LV_COLOR_16_SWAP 1
    tft.writePixels((uint16_t*)&color_p->full, w * h, true, true);
    tft.endWrite();

    lv_disp_flush_ready(disp_drv);
}

// Вспомогательная функция применения тревожного мигания карточки при варнинге
static void applyCardWarning(lv_obj_t* card, bool isAlarm, bool blinkPhase) {
    if (!card) return;
    if (isAlarm && blinkPhase) {
        // Резкая, стробоскопическая заливка всей карточки ярко-красным цветом ///M Red
        lv_obj_set_style_bg_color(card, lv_color_make(220, 20, 30), 0);
        lv_obj_set_style_border_color(card, lv_color_make(255, 255, 255), 0);
        lv_obj_set_style_border_width(card, 3, 0);
    } else {
        lv_obj_set_style_bg_color(card, COLOR_LV_CARD_BG, 0);
        lv_obj_set_style_border_color(card, COLOR_LV_CARD_BORDER, 0);
        lv_obj_set_style_border_width(card, 1, 0);
    }
}

// Вспомогательная функция создания разделительной линии
static lv_obj_t* createDivider(lv_obj_t* parent, int16_t x, int16_t y, int16_t w, int16_t h = 1) {
    lv_obj_t* div = lv_obj_create(parent);
    lv_obj_set_pos(div, x, y);
    lv_obj_set_size(div, w, h);
    lv_obj_set_style_bg_color(div, COLOR_LV_DARK_GRAY, 0);
    lv_obj_set_style_border_width(div, 0, 0);
    lv_obj_set_style_radius(div, 0, 0);
    lv_obj_clear_flag(div, LV_OBJ_FLAG_SCROLLABLE);
    return div;
}

// Вспомогательная функция создания спортивной плитки (карточки)
static lv_obj_t* createCardTile(lv_obj_t* parent, int16_t x, int16_t y, int16_t w, int16_t h) {
    lv_obj_t* tile = lv_obj_create(parent);
    lv_obj_set_pos(tile, x, y);
    lv_obj_set_size(tile, w, h);
    lv_obj_set_style_bg_color(tile, COLOR_LV_CARD_BG, 0);
    lv_obj_set_style_border_color(tile, COLOR_LV_CARD_BORDER, 0);
    lv_obj_set_style_border_width(tile, 1, 0);
    lv_obj_set_style_radius(tile, 8, 0);
    lv_obj_set_style_pad_all(tile, 4, 0);
    lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
    return tile;
}

// Вспомогательная функция создания индикатора MegaSquirt в верхнем левом углу
static lv_obj_t* createMs2StatusLabel(lv_obj_t* parent) {
    lv_obj_t* lbl = lv_label_create(parent);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl, COLOR_LV_OFFLINE, 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 12, 10);
    lv_label_set_text(lbl, LV_SYMBOL_CLOSE " MS2");
    return lbl;
}

// Вспомогательная функция создания индикатора спутников в правом верхнем углу
static lv_obj_t* createGpsCornerLabel(lv_obj_t* parent) {
    lv_obj_t* lbl = lv_label_create(parent);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(lbl, COLOR_LV_MID_GRAY, 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_RIGHT, -12, 10);
    lv_label_set_text(lbl, LV_SYMBOL_GPS " --");
    return lbl;
}

// Обновление состояния индикатора MegaSquirt
static void updateMs2StatusWidget(lv_obj_t* lbl, bool online) {
    if (!lbl) return;
    if (online) {
        lv_obj_set_style_text_color(lbl, COLOR_LV_ONLINE, 0);
        lv_label_set_text(lbl, LV_SYMBOL_OK " MS2");
    } else {
        lv_obj_set_style_text_color(lbl, COLOR_LV_OFFLINE, 0);
        lv_label_set_text(lbl, LV_SYMBOL_CLOSE " MS2");
    }
}

void UiEngine::init() {
    lv_init();

    // Инициализация дисплейного буфера LVGL
    lv_disp_draw_buf_init(&draw_buf, buf_1, NULL, 320 * LV_BUF_LINES);

    // Регистрация драйвера дисплея
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = 320;
    disp_drv.ver_res = 240;
    disp_drv.flush_cb = dispFlushCb;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    // Загрузка сохраненного типа датчика для полноэкранного прибора
    loadGaugeFromNvs();

    // Создание всех экранов интерфейса
    createScreenClock();
    createScreenTrip();
    createScreenTelem();
    createScreenMPerf();
    createScreenGauge();
    createScreenSettings();

    // Загрузка стартового экрана часов
    currentScreen = ScreenId::CLASSIC_CLOCK;
    lv_scr_load(scr_clock);
    lv_obj_invalidate(scr_clock);
    lastTickMillis = millis();
}

// -----------------------------------------------------------------------------
// ЭКРАН 1: ЧАСЫ И ТЕЛЕМЕТРИЯ (CLASSIC CLOCK) - СПОРТИВНЫЕ ПЛИТКИ
// -----------------------------------------------------------------------------
void UiEngine::createScreenClock() {
    scr_clock = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_clock, COLOR_LV_BLACK, 0);
    lv_obj_clear_flag(scr_clock, LV_OBJ_FLAG_SCROLLABLE);

    // Статус MegaSquirt 2 в верхнем левом углу
    lbl_ms2_status_1 = createMs2StatusLabel(scr_clock);

    // Индикатор спутников в правом верхнем углу
    lbl_gps_sats_1 = createGpsCornerLabel(scr_clock);

    // Крупные белые часы (нативный сглаженный 4bpp шрифт 51 px)
    lbl_clock = lv_label_create(scr_clock);
    lv_obj_set_style_text_font(lbl_clock, &lv_font_clock_70, 0);
    lv_obj_set_style_text_color(lbl_clock, COLOR_LV_WHITE, 0);
    lv_obj_align(lbl_clock, LV_ALIGN_TOP_MID, 0, 36);
    lv_label_set_text(lbl_clock, "12:34");

    // ПЛИТКА 1: BOOST (x=10, y=118, w=94, h=110)
    card_boost_1 = createCardTile(scr_clock, 10, 118, 94, 110);
    lv_obj_t* t_boost = lv_label_create(card_boost_1);
    lv_obj_set_style_text_font(t_boost, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(t_boost, COLOR_LV_SILVER, 0);
    lv_obj_align(t_boost, LV_ALIGN_TOP_MID, 0, 4);
    lv_label_set_text(t_boost, "BOOST");

    lbl_boost_val = lv_label_create(card_boost_1);
    lv_obj_set_style_text_font(lbl_boost_val, &lv_font_telemetry_24, 0);
    lv_obj_set_style_text_color(lbl_boost_val, COLOR_LV_WHITE, 0);
    lv_obj_align(lbl_boost_val, LV_ALIGN_CENTER, 0, 2);
    lv_label_set_text(lbl_boost_val, "0.00");

    lbl_boost_sub = lv_label_create(card_boost_1);
    lv_obj_set_style_text_font(lbl_boost_sub, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_boost_sub, COLOR_LV_MID_GRAY, 0);
    lv_obj_align(lbl_boost_sub, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_label_set_text(lbl_boost_sub, "bar");

    // ПЛИТКА 2: COOLANT (x=113, y=118, w=94, h=110)
    card_clt_1 = createCardTile(scr_clock, 113, 118, 94, 110);
    lv_obj_t* t_clt = lv_label_create(card_clt_1);
    lv_obj_set_style_text_font(t_clt, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(t_clt, COLOR_LV_SILVER, 0);
    lv_obj_align(t_clt, LV_ALIGN_TOP_MID, 0, 4);
    lv_label_set_text(t_clt, "COOLANT");

    lbl_clt_val = lv_label_create(card_clt_1);
    lv_obj_set_style_text_font(lbl_clt_val, &lv_font_telemetry_24, 0);
    lv_obj_set_style_text_color(lbl_clt_val, COLOR_LV_WHITE, 0);
    lv_obj_align(lbl_clt_val, LV_ALIGN_CENTER, 0, 2);
    lv_label_set_text(lbl_clt_val, "20");

    lbl_clt_sub = lv_label_create(card_clt_1);
    lv_obj_set_style_text_font(lbl_clt_sub, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_clt_sub, COLOR_LV_MID_GRAY, 0);
    lv_obj_align(lbl_clt_sub, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_label_set_text(lbl_clt_sub, "\xC2\xB0 C");

    // ПЛИТКА 3: AFR (x=216, y=118, w=94, h=110)
    card_afr_1 = createCardTile(scr_clock, 216, 118, 94, 110);
    lv_obj_t* t_afr = lv_label_create(card_afr_1);
    lv_obj_set_style_text_font(t_afr, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(t_afr, COLOR_LV_SILVER, 0);
    lv_obj_align(t_afr, LV_ALIGN_TOP_MID, 0, 4);
    lv_label_set_text(t_afr, "AFR");

    lbl_afr_val = lv_label_create(card_afr_1);
    lv_obj_set_style_text_font(lbl_afr_val, &lv_font_telemetry_24, 0);
    lv_obj_set_style_text_color(lbl_afr_val, COLOR_LV_WHITE, 0);
    lv_obj_align(lbl_afr_val, LV_ALIGN_CENTER, 0, 2);
    lv_label_set_text(lbl_afr_val, "--.-");

    lbl_afr_sub = lv_label_create(card_afr_1);
    lv_obj_set_style_text_font(lbl_afr_sub, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_afr_sub, COLOR_LV_MID_GRAY, 0);
    lv_obj_align(lbl_afr_sub, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_label_set_text(lbl_afr_sub, "");
}

// -----------------------------------------------------------------------------
// ЭКРАН 2: БОРТОВОЙ КОМПЬЮТЕР И РАСХОД (OBC TRIP & FUEL) - 3 СПОРТИВНЫЕ ПЛИТКИ
// -----------------------------------------------------------------------------
void UiEngine::createScreenTrip() {
    scr_trip = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_trip, COLOR_LV_BLACK, 0);
    lv_obj_clear_flag(scr_trip, LV_OBJ_FLAG_SCROLLABLE);

    lbl_ms2_status_2 = createMs2StatusLabel(scr_trip);
    lbl_gps_sats_2 = createGpsCornerLabel(scr_trip);

    // Верхняя плитка скорости и мгновенного расхода
    lv_obj_t* top_card = createCardTile(scr_trip, 10, 32, 300, 66);

    lbl_trip_spd = lv_label_create(top_card);
    lv_obj_set_style_text_font(lbl_trip_spd, &lv_font_clock_70, 0);
    lv_obj_set_style_text_color(lbl_trip_spd, COLOR_LV_WHITE, 0);
    lv_obj_set_pos(lbl_trip_spd, 8, 4);
    lv_label_set_text(lbl_trip_spd, "  0");

    lv_obj_t* t_kmh = lv_label_create(top_card);
    lv_obj_set_style_text_font(t_kmh, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(t_kmh, COLOR_LV_SILVER, 0);
    lv_obj_set_pos(t_kmh, 116, 14);
    lv_label_set_text(t_kmh, "KM/H");

    lbl_trip_fix = lv_label_create(top_card);
    lv_obj_set_style_text_font(lbl_trip_fix, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_trip_fix, COLOR_LV_MID_GRAY, 0);
    lv_obj_set_pos(lbl_trip_fix, 116, 36);
    lv_label_set_text(lbl_trip_fix, "NO FIX");

    lbl_trip_inst = lv_label_create(top_card);
    lv_obj_set_style_text_font(lbl_trip_inst, &lv_font_telemetry_24, 0);
    lv_obj_set_style_text_color(lbl_trip_inst, COLOR_LV_WHITE, 0);
    lv_obj_align(lbl_trip_inst, LV_ALIGN_RIGHT_MID, -12, 0);
    lv_label_set_text(lbl_trip_inst, "0.0 L/H");

    // 3 Спортивные плитки снизу (симметрично экрану часов: 94 x 128 px)
    // Плитка 1: TRIP DISTANCE (слева)
    card_trip_dist = createCardTile(scr_trip, 10, 104, 94, 128);
    lv_obj_t* t_dist = lv_label_create(card_trip_dist);
    lv_obj_set_style_text_font(t_dist, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(t_dist, COLOR_LV_SILVER, 0);
    lv_obj_align(t_dist, LV_ALIGN_TOP_MID, 0, 4);
    lv_label_set_text(t_dist, "TRIP DIST");

    lbl_trip_dist = lv_label_create(card_trip_dist);
    lv_obj_set_style_text_font(lbl_trip_dist, &lv_font_telemetry_24, 0);
    lv_obj_set_style_text_color(lbl_trip_dist, COLOR_LV_WHITE, 0);
    lv_obj_align(lbl_trip_dist, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(lbl_trip_dist, "0.0 km");

    lbl_trip_reset_hint = lv_label_create(card_trip_dist);
    lv_obj_set_style_text_font(lbl_trip_reset_hint, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_trip_reset_hint, COLOR_LV_MID_GRAY, 0);
    lv_obj_align(lbl_trip_reset_hint, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_label_set_text(lbl_trip_reset_hint, "[HOLD RST]");

    // Плитка 2: AVG CONSUMPTION (по центру)
    card_trip_avg = createCardTile(scr_trip, 113, 104, 94, 128);
    lv_obj_t* t_avg = lv_label_create(card_trip_avg);
    lv_obj_set_style_text_font(t_avg, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(t_avg, COLOR_LV_SILVER, 0);
    lv_obj_align(t_avg, LV_ALIGN_TOP_MID, 0, 4);
    lv_label_set_text(t_avg, "AVG CONS");

    lbl_trip_avg = lv_label_create(card_trip_avg);
    lv_obj_set_style_text_font(lbl_trip_avg, &lv_font_telemetry_24, 0);
    lv_obj_set_style_text_color(lbl_trip_avg, COLOR_LV_WHITE, 0);
    lv_obj_align(lbl_trip_avg, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(lbl_trip_avg, "--- L");

    lv_obj_t* sub_avg = lv_label_create(card_trip_avg);
    lv_obj_set_style_text_font(sub_avg, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(sub_avg, COLOR_LV_MID_GRAY, 0);
    lv_obj_align(sub_avg, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_label_set_text(sub_avg, "L/100KM");

    // Плитка 3: TRIP FUEL (справа)
    card_trip_fuel = createCardTile(scr_trip, 216, 104, 94, 128);
    lv_obj_t* t_fuel = lv_label_create(card_trip_fuel);
    lv_obj_set_style_text_font(t_fuel, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(t_fuel, COLOR_LV_SILVER, 0);
    lv_obj_align(t_fuel, LV_ALIGN_TOP_MID, 0, 4);
    lv_label_set_text(t_fuel, "TRIP FUEL");

    lbl_trip_fuel = lv_label_create(card_trip_fuel);
    lv_obj_set_style_text_font(lbl_trip_fuel, &lv_font_telemetry_24, 0);
    lv_obj_set_style_text_color(lbl_trip_fuel, COLOR_LV_WHITE, 0);
    lv_obj_align(lbl_trip_fuel, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(lbl_trip_fuel, "0.0 L");

    lv_obj_t* sub_fuel = lv_label_create(card_trip_fuel);
    lv_obj_set_style_text_font(sub_fuel, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(sub_fuel, COLOR_LV_MID_GRAY, 0);
    lv_obj_align(sub_fuel, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_label_set_text(sub_fuel, "LITERS");
}

void UiEngine::notifyTripReset() {
    if (lbl_trip_reset_hint) {
        lv_label_set_text(lbl_trip_reset_hint, "RESET OK!");
        lv_obj_set_style_text_color(lbl_trip_reset_hint, COLOR_LV_ONLINE, 0);
    }
}

// -----------------------------------------------------------------------------
// ЭКРАН 3: ТЕЛЕМЕТРИЯ МОТОРА (OBC TELEMETRY) - СПОРТИВНЫЕ ПЛИТКИ
// -----------------------------------------------------------------------------
void UiEngine::createScreenTelem() {
    scr_telem = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_telem, COLOR_LV_BLACK, 0);
    lv_obj_clear_flag(scr_telem, LV_OBJ_FLAG_SCROLLABLE);

    lbl_ms2_status_3 = createMs2StatusLabel(scr_telem);
    lbl_gps_sats_3 = createGpsCornerLabel(scr_telem);

    // 1. АКБ (верх-лево)
    lv_obj_t* c_v = createCardTile(scr_telem, 10, 32, 145, 66);
    lv_obj_t* t_v = lv_label_create(c_v);
    lv_obj_set_style_text_font(t_v, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(t_v, COLOR_LV_SILVER, 0);
    lv_obj_align(t_v, LV_ALIGN_TOP_LEFT, 6, 2);
    lv_label_set_text(t_v, "BATTERY");

    lbl_telem_volt = lv_label_create(c_v);
    lv_obj_set_style_text_font(lbl_telem_volt, &lv_font_telemetry_24, 0);
    lv_obj_set_style_text_color(lbl_telem_volt, COLOR_LV_WHITE, 0);
    lv_obj_align(lbl_telem_volt, LV_ALIGN_LEFT_MID, 6, 2);
    lv_label_set_text(lbl_telem_volt, "12.4V");

    lbl_telem_volt_sub = lv_label_create(c_v);
    lv_obj_set_style_text_font(lbl_telem_volt_sub, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_telem_volt_sub, COLOR_LV_SILVER, 0);
    lv_obj_align(lbl_telem_volt_sub, LV_ALIGN_BOTTOM_LEFT, 6, -2);
    lv_label_set_text(lbl_telem_volt_sub, "IGNITION ON");

    // 2. ОЖ (верх-право)
    card_telem_clt = createCardTile(scr_telem, 165, 32, 145, 66);
    lv_obj_t* t_c = lv_label_create(card_telem_clt);
    lv_obj_set_style_text_font(t_c, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(t_c, COLOR_LV_SILVER, 0);
    lv_obj_align(t_c, LV_ALIGN_TOP_LEFT, 6, 2);
    lv_label_set_text(t_c, "COOLANT");

    lbl_telem_clt = lv_label_create(card_telem_clt);
    lv_obj_set_style_text_font(lbl_telem_clt, &lv_font_telemetry_24, 0);
    lv_obj_set_style_text_color(lbl_telem_clt, COLOR_LV_WHITE, 0);
    lv_obj_align(lbl_telem_clt, LV_ALIGN_LEFT_MID, 6, 2);
    lv_label_set_text(lbl_telem_clt, "+25 C");

    lbl_telem_clt_sub = lv_label_create(card_telem_clt);
    lv_obj_set_style_text_font(lbl_telem_clt_sub, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_telem_clt_sub, COLOR_LV_SILVER, 0);
    lv_obj_align(lbl_telem_clt_sub, LV_ALIGN_BOTTOM_LEFT, 6, -2);
    lv_label_set_text(lbl_telem_clt_sub, "INTAKE: +20 C");

    // 3. AFR (низ-лево)
    card_telem_afr = createCardTile(scr_telem, 10, 104, 145, 66);
    lv_obj_t* t_a = lv_label_create(card_telem_afr);
    lv_obj_set_style_text_font(t_a, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(t_a, COLOR_LV_SILVER, 0);
    lv_obj_align(t_a, LV_ALIGN_TOP_LEFT, 6, 2);
    lv_label_set_text(t_a, "AIR / FUEL (AFR)");

    lbl_telem_afr = lv_label_create(card_telem_afr);
    lv_obj_set_style_text_font(lbl_telem_afr, &lv_font_telemetry_24, 0);
    lv_obj_set_style_text_color(lbl_telem_afr, COLOR_LV_WHITE, 0);
    lv_obj_align(lbl_telem_afr, LV_ALIGN_LEFT_MID, 6, 2);
    lv_label_set_text(lbl_telem_afr, "--.-");

    lbl_telem_afr_sub = lv_label_create(card_telem_afr);
    lv_obj_set_style_text_font(lbl_telem_afr_sub, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_telem_afr_sub, COLOR_LV_SILVER, 0);
    lv_obj_align(lbl_telem_afr_sub, LV_ALIGN_BOTTOM_LEFT, 6, -2);
    lv_label_set_text(lbl_telem_afr_sub, "TARGET: --.-");

    // 4. Дроссель TPS (низ-право)
    lv_obj_t* c_t = createCardTile(scr_telem, 165, 104, 145, 66);
    lv_obj_t* t_t = lv_label_create(c_t);
    lv_obj_set_style_text_font(t_t, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(t_t, COLOR_LV_SILVER, 0);
    lv_obj_align(t_t, LV_ALIGN_TOP_LEFT, 6, 2);
    lv_label_set_text(t_t, "THROTTLE (TPS)");

    lbl_telem_tps = lv_label_create(c_t);
    lv_obj_set_style_text_font(lbl_telem_tps, &lv_font_telemetry_24, 0);
    lv_obj_set_style_text_color(lbl_telem_tps, COLOR_LV_WHITE, 0);
    lv_obj_align(lbl_telem_tps, LV_ALIGN_LEFT_MID, 6, 2);
    lv_label_set_text(lbl_telem_tps, "0%");

    lbl_telem_tps_sub = lv_label_create(c_t);
    lv_obj_set_style_text_font(lbl_telem_tps_sub, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_telem_tps_sub, COLOR_LV_SILVER, 0);
    lv_obj_align(lbl_telem_tps_sub, LV_ALIGN_BOTTOM_LEFT, 6, -2);
    lv_label_set_text(lbl_telem_tps_sub, "MAP: 100 kPa");

    // Нижняя шкала наддува
    card_telem_boost = createCardTile(scr_telem, 10, 176, 300, 56);
    lbl_telem_boost_txt = lv_label_create(card_telem_boost);
    lv_obj_set_style_text_font(lbl_telem_boost_txt, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_telem_boost_txt, COLOR_LV_SILVER, 0);
    lv_obj_align(lbl_telem_boost_txt, LV_ALIGN_TOP_LEFT, 8, 2);
    lv_label_set_text(lbl_telem_boost_txt, "BOOST: 0.00 Bar (100 kPa)");

    bar_telem_boost = lv_bar_create(card_telem_boost);
    lv_obj_set_size(bar_telem_boost, 280, 16);
    lv_obj_align(bar_telem_boost, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_bar_set_range(bar_telem_boost, 0, 230); // -0.8 .. +1.5 bar
    lv_bar_set_value(bar_telem_boost, 80, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar_telem_boost, COLOR_LV_DARK_GRAY, 0);
    lv_obj_set_style_bg_color(bar_telem_boost, COLOR_LV_WHITE, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar_telem_boost, 3, 0);
    lv_obj_set_style_radius(bar_telem_boost, 3, LV_PART_INDICATOR);
}

// -----------------------------------------------------------------------------
// ЭКРАН 4: ///M PERFORMANCE & ТАХОМЕТР - СПОРТИВНЫЕ ПЛИТКИ
// -----------------------------------------------------------------------------
void UiEngine::createScreenMPerf() {
    scr_m_perf = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_m_perf, COLOR_LV_BLACK, 0);
    lv_obj_clear_flag(scr_m_perf, LV_OBJ_FLAG_SCROLLABLE);

    lbl_ms2_status_4 = createMs2StatusLabel(scr_m_perf);
    lbl_gps_sats_4 = createGpsCornerLabel(scr_m_perf);

    // Верхняя карточка тахометра
    lv_obj_t* c_tacho = createCardTile(scr_m_perf, 10, 32, 300, 116);

    lbl_m_rpm = lv_label_create(c_tacho);
    lv_obj_set_style_text_font(lbl_m_rpm, &lv_font_clock_70, 0);
    lv_obj_set_style_text_color(lbl_m_rpm, COLOR_LV_WHITE, 0);
    lv_obj_set_pos(lbl_m_rpm, 12, 6);
    lv_label_set_text(lbl_m_rpm, "0");

    lv_obj_t* t_rpm = lv_label_create(c_tacho);
    lv_obj_set_style_text_font(t_rpm, &lv_font_telemetry_24, 0);
    lv_obj_set_style_text_color(t_rpm, COLOR_LV_SILVER, 0);
    lv_obj_set_pos(t_rpm, 180, 28);
    lv_label_set_text(t_rpm, "RPM");

    // Прогрессивная полоса тахометра
    bar_m_shift = lv_bar_create(c_tacho);
    lv_obj_set_size(bar_m_shift, 276, 22);
    lv_obj_set_pos(bar_m_shift, 10, 68);
    lv_bar_set_range(bar_m_shift, 0, 7500);
    lv_bar_set_value(bar_m_shift, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar_m_shift, COLOR_LV_DARK_GRAY, 0);
    lv_obj_set_style_bg_color(bar_m_shift, COLOR_LV_WHITE, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar_m_shift, 3, 0);
    lv_obj_set_style_radius(bar_m_shift, 3, LV_PART_INDICATOR);

    // Метки шкалы
    lv_obj_t* t_scale = lv_label_create(c_tacho);
    lv_obj_set_style_text_font(t_scale, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(t_scale, COLOR_LV_SILVER, 0);
    lv_obj_set_pos(t_scale, 12, 94);
    lv_label_set_text(t_scale, "0         2k        4k        6k       7.5k");

    // Статус ожидания CAN
    lbl_m_offline = lv_label_create(c_tacho);
    lv_obj_set_style_text_font(lbl_m_offline, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_m_offline, COLOR_LV_MID_GRAY, 0);
    lv_obj_align(lbl_m_offline, LV_ALIGN_CENTER, 0, -10);
    lv_label_set_text(lbl_m_offline, "WAITING MS2 CAN...");
    lv_obj_add_flag(lbl_m_offline, LV_OBJ_FLAG_HIDDEN);

    // Нижние 2 плитки
    // 1. Наддув
    card_m_boost = createCardTile(scr_m_perf, 10, 154, 145, 78);
    lv_obj_t* t_mb = lv_label_create(card_m_boost);
    lv_obj_set_style_text_font(t_mb, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(t_mb, COLOR_LV_SILVER, 0);
    lv_obj_align(t_mb, LV_ALIGN_TOP_LEFT, 6, 2);
    lv_label_set_text(t_mb, "BOOST");

    lbl_m_boost = lv_label_create(card_m_boost);
    lv_obj_set_style_text_font(lbl_m_boost, &lv_font_telemetry_24, 0);
    lv_obj_set_style_text_color(lbl_m_boost, COLOR_LV_WHITE, 0);
    lv_obj_align(lbl_m_boost, LV_ALIGN_BOTTOM_LEFT, 6, -6);
    lv_label_set_text(lbl_m_boost, "0.00b");

    // 2. УОЗ
    lv_obj_t* c_ma = createCardTile(scr_m_perf, 165, 154, 145, 78);
    lv_obj_t* t_ma = lv_label_create(c_ma);
    lv_obj_set_style_text_font(t_ma, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(t_ma, COLOR_LV_SILVER, 0);
    lv_obj_align(t_ma, LV_ALIGN_TOP_LEFT, 6, 2);
    lv_label_set_text(t_ma, "IGN ADVANCE");

    lbl_m_adv = lv_label_create(c_ma);
    lv_obj_set_style_text_font(lbl_m_adv, &lv_font_telemetry_24, 0);
    lv_obj_set_style_text_color(lbl_m_adv, COLOR_LV_WHITE, 0);
    lv_obj_align(lbl_m_adv, LV_ALIGN_BOTTOM_LEFT, 6, -6);
    lv_label_set_text(lbl_m_adv, "10.0*");
}

// -----------------------------------------------------------------------------
// ЭКРАН 5: НАСТРОЙКИ И ИНФО (SETTINGS & INFO) - СПОРТИВНЫЕ ПЛИТКИ
// -----------------------------------------------------------------------------
void UiEngine::createScreenSettings() {
    scr_settings = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_settings, COLOR_LV_BLACK, 0);
    lv_obj_clear_flag(scr_settings, LV_OBJ_FLAG_SCROLLABLE);

    // Секция 1: Bluetooth
    lv_obj_t* c1 = createCardTile(scr_settings, 10, 14, 300, 48);
    lbl_set_ble = lv_label_create(c1);
    lv_obj_set_style_text_font(lbl_set_ble, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_set_ble, COLOR_LV_WHITE, 0);
    lv_obj_align(lbl_set_ble, LV_ALIGN_LEFT_MID, 8, 0);
    lv_label_set_text(lbl_set_ble, LV_SYMBOL_BLUETOOTH " BLE: BMW_OBC_E36 (READY)");

    // Секция 2: CAN-шина MegaSquirt 2
    lv_obj_t* c2 = createCardTile(scr_settings, 10, 68, 300, 48);
    lbl_set_can = lv_label_create(c2);
    lv_obj_set_style_text_font(lbl_set_can, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_set_can, COLOR_LV_WHITE, 0);
    lv_obj_align(lbl_set_can, LV_ALIGN_LEFT_MID, 8, 0);
    lv_label_set_text(lbl_set_can, "CAN-BUS: 500 kbps (OFFLINE)");

    // Секция 3: GPS модуль
    lv_obj_t* c3 = createCardTile(scr_settings, 10, 122, 300, 48);
    lbl_set_gps = lv_label_create(c3);
    lv_obj_set_style_text_font(lbl_set_gps, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_set_gps, COLOR_LV_WHITE, 0);
    lv_obj_align(lbl_set_gps, LV_ALIGN_LEFT_MID, 8, 0);
    lv_label_set_text(lbl_set_gps, LV_SYMBOL_GPS " GPS: NEO-7M (NO FIX)");

    // Секция 4: Версия прошивки
    lv_obj_t* c4 = createCardTile(scr_settings, 10, 176, 300, 48);
    lbl_set_sys = lv_label_create(c4);
    lv_obj_set_style_text_font(lbl_set_sys, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_set_sys, COLOR_LV_SILVER, 0);
    lv_obj_align(lbl_set_sys, LV_ALIGN_LEFT_MID, 8, 0);
    lv_label_set_text(lbl_set_sys, "BMW E36 OBC v2.4 (LVGL 8.3) - ESP32-S3");
}

void UiEngine::showBootSplash(const char* subtitle) {
    Display.setBrightness(DEFAULT_BRIGHTNESS);
    Adafruit_ST7789& tft = Display.getTft();
    tft.fillScreen(0x0000);

    // Лаконичная заставка Megasquirt 2
    tft.setFont(&FreeSansBoldOblique12pt7b);
    tft.setTextColor(0xFFFF);
    tft.setCursor(68, 105);
    tft.print("Megasquirt 2");
    tft.drawFastHLine(48, 120, 224, 0x4208);
    tft.setFont();

    if (subtitle && subtitle[0]) {
        tft.setTextColor(0xAD55); // серый цвет
        tft.setTextSize(1);
        int16_t x = 160 - (strlen(subtitle) * 6) / 2;
        if (x < 10) x = 10;
        tft.setCursor(x, 132);
        tft.print(subtitle);
    }

    delay(900);
    tft.fillScreen(0x0000);
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
    if (currentScreen != screen) {
        currentScreen = screen;
        lv_obj_t* target = nullptr;
        switch (screen) {
            case ScreenId::CLASSIC_CLOCK:  target = scr_clock; break;
            case ScreenId::OBC_TRIP_FUEL:  target = scr_trip; break;
            case ScreenId::OBC_TELEMETRY:  target = scr_telem; break;
            case ScreenId::M_PERFORMANCE:  target = scr_m_perf; break;
            case ScreenId::SINGLE_GAUGE:   target = scr_gauge; break;
            case ScreenId::SETTINGS_INFO:  target = scr_settings; break;
            default: target = scr_clock; break;
        }
        if (target) {
            lv_scr_load(target);
            lv_obj_invalidate(target);
        }
    }
}

// -----------------------------------------------------------------------------
// Главный метод периодического обновления UI
// -----------------------------------------------------------------------------
void UiEngine::update() {
    unsigned long now = millis();
    lv_tick_inc(now - lastTickMillis);
    lastTickMillis = now;

    // Мигание двоеточия раз в секунду
    if (now - lastColonBlinkMillis >= 500) {
        colonState = !colonState;
        lastColonBlinkMillis = now;
    }

    // Обновляем данные для текущего активного экрана
    switch (currentScreen) {
        case ScreenId::CLASSIC_CLOCK:
            updateClockScreen();
            break;
        case ScreenId::OBC_TRIP_FUEL:
            updateTripScreen();
            break;
        case ScreenId::OBC_TELEMETRY:
            updateTelemScreen();
            break;
        case ScreenId::M_PERFORMANCE:
            updateMPerfScreen();
            break;
        case ScreenId::SINGLE_GAUGE:
            updateGaugeScreen();
            break;
        case ScreenId::SETTINGS_INFO:
            updateSettingsScreen();
            break;
        default:
            break;
    }

    // Запуск цикла отрисовки и событий LVGL
    lv_timer_handler();
}

void UiEngine::updateClockScreen() {
    const TimeData& td = Time.getTime();
    const SensorData& sens = Sensors.getData();
    const GpsData& gps = Gps.getData();
    const WarningState& warn = Warnings.getState();

    // Применение тревожного мигания карточек при аварийных ситуациях
    applyCardWarning(card_boost_1, warn.boostAlarm, warn.blinkPhase);
    applyCardWarning(card_clt_1, warn.cltAlarm, warn.blinkPhase);
    applyCardWarning(card_afr_1, warn.afrAlarm, warn.blinkPhase);

    // Статус MegaSquirt 2 в верхнем левом углу
    updateMs2StatusWidget(lbl_ms2_status_1, sens.ms2Online);

    // Часы с мигающим двоеточием (крупный 4bpp шрифт 51 px)
    if (colonState) {
        lv_label_set_text_fmt(lbl_clock, "%02d:%02d", td.hour, td.minute);
    } else {
        lv_label_set_text_fmt(lbl_clock, "%02d %02d", td.hour, td.minute);
    }

    // Количество спутников в правом верхнем углу
    if (gps.hasFix) {
        lv_obj_set_style_text_color(lbl_gps_sats_1, COLOR_LV_WHITE, 0);
        lv_label_set_text_fmt(lbl_gps_sats_1, LV_SYMBOL_GPS " %d", gps.satellites);
    } else {
        lv_obj_set_style_text_color(lbl_gps_sats_1, COLOR_LV_MID_GRAY, 0);
        if (gps.satellites > 0) {
            lv_label_set_text_fmt(lbl_gps_sats_1, LV_SYMBOL_GPS " %d", gps.satellites);
        } else {
            lv_label_set_text(lbl_gps_sats_1, LV_SYMBOL_GPS " --");
        }
    }

    // Плитки: BOOST, COOLANT, AFR
    if (sens.ms2Online) {
        // Чистые цифры наддува (%.2f), снизу bar или OVERBOOST!
        lv_label_set_text_fmt(lbl_boost_val, "%.2f", sens.ms2.boost_bar);
        if (warn.boostAlarm) {
            lv_obj_set_style_text_color(lbl_boost_sub, COLOR_LV_M_RED, 0);
            lv_label_set_text(lbl_boost_sub, "OVERBOOST!");
        } else {
            lv_obj_set_style_text_color(lbl_boost_sub, COLOR_LV_MID_GRAY, 0);
            lv_label_set_text(lbl_boost_sub, "bar");
        }

        // Чистые цифры температуры ОЖ (%d), снизу °C или OVERHEAT!
        lv_label_set_text_fmt(lbl_clt_val, "%d", (int)round(sens.ms2.clt_c));
        if (warn.cltAlarm) {
            lv_obj_set_style_text_color(lbl_clt_sub, COLOR_LV_M_RED, 0);
            lv_label_set_text(lbl_clt_sub, "OVERHEAT!");
        } else {
            lv_obj_set_style_text_color(lbl_clt_sub, COLOR_LV_MID_GRAY, 0);
            lv_label_set_text(lbl_clt_sub, "\xC2\xB0 C");
        }

        // Чистые цифры AFR (%.1f), снизу пусто или LEAN! / RICH!
        lv_label_set_text_fmt(lbl_afr_val, "%.1f", sens.ms2.afr);
        if (warn.afrAlarm) {
            lv_obj_set_style_text_color(lbl_afr_sub, COLOR_LV_M_RED, 0);
            lv_label_set_text(lbl_afr_sub, (sens.ms2.afr >= Warnings.getSettings().afr_lean_max) ? "LEAN!" : "RICH!");
        } else {
            lv_label_set_text(lbl_afr_sub, "");
        }
    } else {
        lv_label_set_text(lbl_boost_val, "0.00");
        lv_obj_set_style_text_color(lbl_boost_sub, COLOR_LV_MID_GRAY, 0);
        lv_label_set_text(lbl_boost_sub, "bar");

        lv_label_set_text_fmt(lbl_clt_val, "%d", (int)round(sens.tempOutdoor));
        lv_obj_set_style_text_color(lbl_clt_sub, COLOR_LV_MID_GRAY, 0);
        lv_label_set_text(lbl_clt_sub, "\xC2\xB0 C");

        lv_label_set_text(lbl_afr_val, "--.-");
        lv_label_set_text(lbl_afr_sub, "");
    }
}

void UiEngine::updateTripScreen() {
    const TripData& trip = Trip.getData();
    const SensorData& sens = Sensors.getData();
    const GpsData& gps = Gps.getData();

    // Статус MegaSquirt 2 в верхнем левом углу
    updateMs2StatusWidget(lbl_ms2_status_2, sens.ms2Online);

    // Скорость по GPS
    int spd = (int)trip.current_speed_kmh;
    if (spd > 999) spd = 999;
    lv_label_set_text_fmt(lbl_trip_spd, "%3d", spd);

    // Статус фиксации GPS
    if (gps.hasFix) {
        lv_label_set_text(lbl_trip_fix, "");
        lv_obj_set_style_text_color(lbl_gps_sats_2, COLOR_LV_WHITE, 0);
        lv_label_set_text_fmt(lbl_gps_sats_2, LV_SYMBOL_GPS " %d", gps.satellites);
    } else {
        lv_label_set_text(lbl_trip_fix, "NO FIX");
        lv_obj_set_style_text_color(lbl_gps_sats_2, COLOR_LV_MID_GRAY, 0);
        if (gps.satellites > 0) {
            lv_label_set_text_fmt(lbl_gps_sats_2, LV_SYMBOL_GPS " %d", gps.satellites);
        } else {
            lv_label_set_text(lbl_gps_sats_2, LV_SYMBOL_GPS " --");
        }
    }

    // Мгновенный расход
    char instBuf[32];
    snprintf(instBuf, sizeof(instBuf), "%.1f %s", trip.instant_consumption, trip.isLitersPerHour ? "L/H" : "L");
    lv_label_set_text(lbl_trip_inst, instBuf);

    // 3 Спортивные плитки
    lv_label_set_text_fmt(lbl_trip_dist, "%.1f km", trip.trip_distance_km);
    if (trip.trip_distance_km >= 0.1f) {
        lv_label_set_text_fmt(lbl_trip_avg, "%.1f", trip.avg_consumption_l_100km);
    } else {
        lv_label_set_text(lbl_trip_avg, "---");
    }
    lv_label_set_text_fmt(lbl_trip_fuel, "%.1f L", trip.trip_fuel_liters);
}

void UiEngine::updateTelemScreen() {
    const SensorData& sens = Sensors.getData();
    const GpsData& gps = Gps.getData();
    const WarningState& warn = Warnings.getState();

    // Применение тревожного мигания карточек
    applyCardWarning(card_telem_clt, warn.cltAlarm, warn.blinkPhase);
    applyCardWarning(card_telem_afr, warn.afrAlarm, warn.blinkPhase);
    applyCardWarning(card_telem_boost, warn.boostAlarm, warn.blinkPhase);

    // Статус MegaSquirt 2 в верхнем левом углу
    updateMs2StatusWidget(lbl_ms2_status_3, sens.ms2Online);

    // Спутники
    if (gps.hasFix) {
        lv_obj_set_style_text_color(lbl_gps_sats_3, COLOR_LV_WHITE, 0);
        lv_label_set_text_fmt(lbl_gps_sats_3, LV_SYMBOL_GPS " %d", gps.satellites);
    } else {
        lv_obj_set_style_text_color(lbl_gps_sats_3, COLOR_LV_MID_GRAY, 0);
        if (gps.satellites > 0) {
            lv_label_set_text_fmt(lbl_gps_sats_3, LV_SYMBOL_GPS " %d", gps.satellites);
        } else {
            lv_label_set_text(lbl_gps_sats_3, LV_SYMBOL_GPS " --");
        }
    }

    // 1. АКБ
    lv_label_set_text_fmt(lbl_telem_volt, "%.2fV", sens.batteryVoltage);
    if (sens.voltStatus == VoltageStatus::VOLT_CRITICAL_LOW) {
        lv_obj_set_style_text_color(lbl_telem_volt_sub, COLOR_LV_M_RED, 0);
        lv_label_set_text(lbl_telem_volt_sub, "LOW BATTERY!");
    } else if (sens.voltStatus == VoltageStatus::VOLT_NORMAL_RUNNING) {
        lv_obj_set_style_text_color(lbl_telem_volt_sub, COLOR_LV_SILVER, 0);
        lv_label_set_text(lbl_telem_volt_sub, "ALT: CHARGING");
    } else {
        lv_obj_set_style_text_color(lbl_telem_volt_sub, COLOR_LV_SILVER, 0);
        lv_label_set_text_fmt(lbl_telem_volt_sub, "CRK: %.1fV", sens.minCrankVoltage);
    }

    // 2. ОЖ
    if (sens.ms2Online) {
        lv_label_set_text_fmt(lbl_telem_clt, "%+.0f C", sens.ms2.clt_c);
        lv_label_set_text_fmt(lbl_telem_clt_sub, "INTAKE: %+.0f C", sens.ms2.mat_c);
    } else {
        lv_label_set_text_fmt(lbl_telem_clt, "%+.0f C", sens.tempOutdoor);
        lv_label_set_text_fmt(lbl_telem_clt_sub, "INTAKE: %+.0f C", sens.tempCabin);
    }

    // 3. AFR
    if (sens.ms2Online) {
        lv_label_set_text_fmt(lbl_telem_afr, "%.1f", sens.ms2.afr);
        lv_label_set_text_fmt(lbl_telem_afr_sub, "TARGET: %.1f", sens.ms2.afr_target);
    } else {
        lv_label_set_text(lbl_telem_afr, "--.-");
        lv_label_set_text(lbl_telem_afr_sub, "NO CAN DATA");
    }

    // 4. Дроссель
    if (sens.ms2Online) {
        lv_label_set_text_fmt(lbl_telem_tps, "%.0f%%", sens.ms2.tps_pct);
        lv_label_set_text_fmt(lbl_telem_tps_sub, "MAP: %.0f kPa", sens.ms2.map_kpa);
    } else {
        lv_label_set_text(lbl_telem_tps, "--%%");
        lv_label_set_text(lbl_telem_tps_sub, "TPS NO CAN");
    }

    // Нижняя полоса наддува
    float boostVal = sens.ms2Online ? sens.ms2.boost_bar : 0.0f;
    int mapKpa = sens.ms2Online ? (int)sens.ms2.map_kpa : 100;
    lv_label_set_text_fmt(lbl_telem_boost_txt, "BOOST: %+.2f Bar (%d kPa)", boostVal, mapKpa);

    int barVal = map(constrain((int)(boostVal * 100), -80, 150), -80, 150, 0, 230);
    lv_bar_set_value(bar_telem_boost, barVal, LV_ANIM_OFF);
}

void UiEngine::updateMPerfScreen() {
    const SensorData& sens = Sensors.getData();
    const GpsData& gps = Gps.getData();
    const WarningState& warn = Warnings.getState();

    // Применение тревожного мигания карточки наддува
    applyCardWarning(card_m_boost, warn.boostAlarm, warn.blinkPhase);

    // Статус MegaSquirt 2 в верхнем левом углу
    updateMs2StatusWidget(lbl_ms2_status_4, sens.ms2Online);

    // Спутники
    if (gps.hasFix) {
        lv_obj_set_style_text_color(lbl_gps_sats_4, COLOR_LV_WHITE, 0);
        lv_label_set_text_fmt(lbl_gps_sats_4, LV_SYMBOL_GPS " %d", gps.satellites);
    } else {
        lv_obj_set_style_text_color(lbl_gps_sats_4, COLOR_LV_MID_GRAY, 0);
        if (gps.satellites > 0) {
            lv_label_set_text_fmt(lbl_gps_sats_4, LV_SYMBOL_GPS " %d", gps.satellites);
        } else {
            lv_label_set_text(lbl_gps_sats_4, LV_SYMBOL_GPS " --");
        }
    }

    if (sens.ms2Online) {
        lv_obj_add_flag(lbl_m_offline, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(lbl_m_rpm, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(bar_m_shift, LV_OBJ_FLAG_HIDDEN);

        lv_label_set_text_fmt(lbl_m_rpm, "%4d", sens.ms2.rpm);

        // Тахометр Shift-Bar
        lv_bar_set_value(bar_m_shift, constrain((int)sens.ms2.rpm, 0, 7500), LV_ANIM_OFF);
        if (sens.ms2.rpm >= 6500) {
            lv_obj_set_style_bg_color(bar_m_shift, COLOR_LV_M_RED, LV_PART_INDICATOR);
        } else {
            lv_obj_set_style_bg_color(bar_m_shift, COLOR_LV_WHITE, LV_PART_INDICATOR);
        }

        lv_label_set_text_fmt(lbl_m_boost, "%+.2fb", sens.ms2.boost_bar);
        lv_label_set_text_fmt(lbl_m_adv, "%.1f*", sens.ms2.advance_deg);
    } else {
        lv_obj_clear_flag(lbl_m_offline, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(lbl_m_rpm, "0");
        lv_bar_set_value(bar_m_shift, 0, LV_ANIM_OFF);
    }
}

void UiEngine::updateSettingsScreen() {
    const SensorData& sens = Sensors.getData();
    const GpsData& gps = Gps.getData();

    lv_label_set_text_fmt(lbl_set_ble, LV_SYMBOL_BLUETOOTH " BLE: BMW_OBC_E36 (%s)",
                          BleMgr.isConnected() ? "CONNECTED" : "READY");

    lv_label_set_text_fmt(lbl_set_can, "CAN-BUS: 500 kbps (%s, %lu pkts)",
                          sens.ms2Online ? "ONLINE" : "OFFLINE", CanBus.getTelemetry().packetsTotal);

    lv_label_set_text_fmt(lbl_set_gps, LV_SYMBOL_GPS " GPS: NEO-7M (%s, %d SATS)",
                          gps.hasFix ? "3D FIX" : "SEARCHING", gps.satellites);
}

// -----------------------------------------------------------------------------
// ЭКРАН 5: ПОЛНОЭКРАННЫЙ ЦИФРОВОЙ ПРИБОР (SINGLE DIGITAL GAUGE)
// -----------------------------------------------------------------------------
void UiEngine::createScreenGauge() {
    scr_gauge = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_gauge, COLOR_LV_BLACK, 0);
    lv_obj_clear_flag(scr_gauge, LV_OBJ_FLAG_SCROLLABLE);

    // Крупная спортивная карточка на весь экран (300 x 220 px)
    card_digital_gauge = createCardTile(scr_gauge, 10, 10, 300, 220);

    // Сверху: Название прибора (шрифт Montserrat 20)
    lbl_digital_title = lv_label_create(card_digital_gauge);
    lv_obj_set_style_text_font(lbl_digital_title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lbl_digital_title, COLOR_LV_SILVER, 0);
    lv_obj_align(lbl_digital_title, LV_ALIGN_TOP_MID, 0, 6);
    lv_label_set_text(lbl_digital_title, "BOOST / TURBO");

    // По центру: Максимально крупное цифровое значение (нативный 4bpp шрифт 96 px)
    lbl_digital_val = lv_label_create(card_digital_gauge);
    lv_obj_set_style_text_font(lbl_digital_val, &lv_font_clock_96, 0);
    lv_obj_set_style_text_color(lbl_digital_val, COLOR_LV_WHITE, 0);
    lv_obj_align(lbl_digital_val, LV_ALIGN_CENTER, 0, -10);
    lv_label_set_text(lbl_digital_val, "0.00");

    // Под цифрами: Единица измерения
    lbl_digital_unit = lv_label_create(card_digital_gauge);
    lv_obj_set_style_text_font(lbl_digital_unit, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_digital_unit, COLOR_LV_MID_GRAY, 0);
    lv_obj_align(lbl_digital_unit, LV_ALIGN_CENTER, 0, 42);
    lv_label_set_text(lbl_digital_unit, "BAR");

    // Внизу слева: Пиковое значение за поездку (BMW Amber / Янтарный)
    lbl_digital_peak = lv_label_create(card_digital_gauge);
    lv_obj_set_size(lbl_digital_peak, 140, 20);
    lv_obj_set_pos(lbl_digital_peak, 8, 188);
    lv_obj_set_style_text_font(lbl_digital_peak, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_digital_peak, lv_color_make(255, 170, 0), 0);
    lv_obj_set_style_text_align(lbl_digital_peak, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_long_mode(lbl_digital_peak, LV_LABEL_LONG_CLIP);
    lv_label_set_text(lbl_digital_peak, "PK: +0.00b");

    // Внизу справа: Сохраненные лимиты варнингов для данного прибора
    lbl_digital_warn_limit = lv_label_create(card_digital_gauge);
    lv_obj_set_size(lbl_digital_warn_limit, 140, 20);
    lv_obj_set_pos(lbl_digital_warn_limit, 144, 188);
    lv_obj_set_style_text_font(lbl_digital_warn_limit, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_digital_warn_limit, COLOR_LV_SILVER, 0);
    lv_obj_set_style_text_align(lbl_digital_warn_limit, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(lbl_digital_warn_limit, LV_LABEL_LONG_CLIP);
    lv_label_set_text(lbl_digital_warn_limit, "LIM: >1.20b");
}

void UiEngine::setGaugeType(GaugeType type) {
    if (type >= GaugeType::COUNT) type = GaugeType::BOOST;
    currentGauge = type;
    saveGaugeToNvs();
    if (currentScreen == ScreenId::SINGLE_GAUGE) {
        updateGaugeScreen();
    }
}

void UiEngine::nextGaugeType() {
    int next = ((int)currentGauge + 1);
    if (next >= (int)GaugeType::COUNT) {
        next = 0;
    }
    setGaugeType((GaugeType)next);
    Serial.printf("[UI] Переключение типа цифрового прибора: %d\n", next);
}

void UiEngine::updateGaugeScreen() {
    const SensorData& sens = Sensors.getData();
    const TripData& trip = Trip.getData();
    const TripPeaks& peaks = Trip.getPeaks();
    const WarningState& warn = Warnings.getState();
    const WarningSettings& ws = Warnings.getSettings();

    bool isAlarm = false;
    char valBuf[32] = {0};
    char peakBuf[48] = {0};
    char limitBuf[48] = {0};

    switch (currentGauge) {
        case GaugeType::BOOST: {
            lv_label_set_text(lbl_digital_title, "BOOST / TURBO");
            lv_label_set_text(lbl_digital_unit, "BAR");
            float b = sens.ms2Online ? sens.ms2.boost_bar : 0.0f;
            snprintf(valBuf, sizeof(valBuf), "%+.2f", b);
            snprintf(peakBuf, sizeof(peakBuf), "PK: %+.2fb", peaks.peak_boost_bar);
            snprintf(limitBuf, sizeof(limitBuf), "LIM: >%.2fb", ws.boost_max_bar);
            isAlarm = warn.boostAlarm;
            break;
        }

        case GaugeType::COOLANT: {
            lv_label_set_text(lbl_digital_title, "COOLANT TEMP");
            lv_label_set_text(lbl_digital_unit, "\xC2\xB0 C");
            float clt = sens.ms2Online ? sens.ms2.clt_c : sens.tempOutdoor;
            snprintf(valBuf, sizeof(valBuf), "%d", (int)round(clt));
            snprintf(peakBuf, sizeof(peakBuf), "PK: %d\xC2\xB0 C", (int)round(peaks.max_clt_c));
            snprintf(limitBuf, sizeof(limitBuf), "LIM: >%d\xC2\xB0 C", (int)round(ws.clt_max_c));
            isAlarm = warn.cltAlarm;
            break;
        }

        case GaugeType::AFR: {
            lv_label_set_text(lbl_digital_title, "AIR / FUEL RATIO");
            lv_label_set_text(lbl_digital_unit, "AFR");
            if (sens.ms2Online) {
                snprintf(valBuf, sizeof(valBuf), "%.1f", sens.ms2.afr);
            } else {
                snprintf(valBuf, sizeof(valBuf), "--.-");
            }
            if (peaks.min_afr <= 30.0f && peaks.max_afr >= 5.0f) {
                snprintf(peakBuf, sizeof(peakBuf), "PK: %.1f/%.1f", peaks.min_afr, peaks.max_afr);
            } else {
                snprintf(peakBuf, sizeof(peakBuf), "PK: --.-");
            }
            snprintf(limitBuf, sizeof(limitBuf), "LIM: %.1f-%.1f", ws.afr_rich_min, ws.afr_lean_max);
            isAlarm = warn.afrAlarm;
            break;
        }

        case GaugeType::INST_FUEL: {
            lv_label_set_text(lbl_digital_title, "INSTANT FUEL");
            lv_label_set_text(lbl_digital_unit, trip.isLitersPerHour ? "L / HOUR" : "L / 100KM");
            snprintf(valBuf, sizeof(valBuf), "%.1f", trip.instant_consumption);
            snprintf(peakBuf, sizeof(peakBuf), "PK: %.1f L", peaks.peak_instant_fuel);
            snprintf(limitBuf, sizeof(limitBuf), "LIM: >25.0 L");
            isAlarm = (trip.instant_consumption >= 25.0f && !trip.isLitersPerHour);
            break;
        }

        case GaugeType::SPEED: {
            lv_label_set_text(lbl_digital_title, "GPS SPEED");
            lv_label_set_text(lbl_digital_unit, "KM / H");
            snprintf(valBuf, sizeof(valBuf), "%d", (int)round(trip.current_speed_kmh));
            snprintf(peakBuf, sizeof(peakBuf), "MAX: %d KM/H", (int)round(peaks.max_speed_kmh));
            snprintf(limitBuf, sizeof(limitBuf), "LIM: >140");
            isAlarm = (trip.current_speed_kmh >= 140.0f);
            break;
        }

        case GaugeType::INTAKE_TEMP: {
            lv_label_set_text(lbl_digital_title, "INTAKE AIR TEMP");
            lv_label_set_text(lbl_digital_unit, "\xC2\xB0 C");
            float mat = sens.ms2Online ? sens.ms2.mat_c : sens.tempCabin;
            snprintf(valBuf, sizeof(valBuf), "%d", (int)round(mat));
            snprintf(peakBuf, sizeof(peakBuf), "PK: %d\xC2\xB0 C", (int)round(peaks.max_iat_c));
            snprintf(limitBuf, sizeof(limitBuf), "LIM: >55\xC2\xB0 C");
            isAlarm = (mat >= 55.0f);
            break;
        }

        case GaugeType::RPM: {
            lv_label_set_text(lbl_digital_title, "TACHOMETER");
            lv_label_set_text(lbl_digital_unit, "RPM");
            uint16_t rpm = sens.ms2Online ? sens.ms2.rpm : 0;
            snprintf(valBuf, sizeof(valBuf), "%d", rpm);
            snprintf(peakBuf, sizeof(peakBuf), "PK: %d RPM", peaks.max_rpm);
            snprintf(limitBuf, sizeof(limitBuf), "REDLINE: >6500");
            isAlarm = (rpm >= 6500);
            break;
        }

        default:
            break;
    }

    lv_label_set_text(lbl_digital_val, valBuf);
    lv_label_set_text(lbl_digital_peak, peakBuf);
    lv_label_set_text(lbl_digital_warn_limit, limitBuf);

    // При аварии/варнинге карточка резко стробирует ярко-красным ///M Red с белой обводкой!
    applyCardWarning(card_digital_gauge, isAlarm, warn.blinkPhase);

    if (isAlarm && warn.blinkPhase) {
        lv_obj_set_style_text_color(lbl_digital_val, COLOR_LV_WHITE, 0);
        lv_obj_set_style_text_color(lbl_digital_title, COLOR_LV_WHITE, 0);
        lv_obj_set_style_text_color(lbl_digital_peak, COLOR_LV_WHITE, 0);
        lv_obj_set_style_text_color(lbl_digital_warn_limit, COLOR_LV_WHITE, 0);
    } else {
        lv_obj_set_style_text_color(lbl_digital_val, isAlarm ? COLOR_LV_M_RED : COLOR_LV_WHITE, 0);
        lv_obj_set_style_text_color(lbl_digital_title, COLOR_LV_SILVER, 0);
        lv_obj_set_style_text_color(lbl_digital_peak, lv_color_make(255, 170, 0), 0);
        lv_obj_set_style_text_color(lbl_digital_warn_limit, isAlarm ? COLOR_LV_M_RED : COLOR_LV_SILVER, 0);
    }
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
