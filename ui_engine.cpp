#include "ui_engine.h"
#include "ble_manager.h"
#include <Fonts/FreeSansBoldOblique12pt7b.h>

UiEngine UI;

// Цвета BMW
#define COLOR_LV_WHITE        lv_color_make(255, 255, 255)
#define COLOR_LV_BLACK        lv_color_make(0, 0, 0)
#define COLOR_LV_SILVER       lv_color_make(180, 180, 180)
#define COLOR_LV_DARK_GRAY    lv_color_make(50, 50, 50)
#define COLOR_LV_MID_GRAY     lv_color_make(120, 120, 120)
#define COLOR_LV_M_RED        lv_color_make(220, 30, 30)

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
      scr_settings(nullptr),
      lbl_clock(nullptr),
      lbl_gps_sats_1(nullptr),
      lbl_boost_val(nullptr),
      lbl_clt_val(nullptr),
      lbl_afr_val(nullptr),
      lbl_trip_spd(nullptr),
      lbl_trip_fix(nullptr),
      lbl_trip_inst(nullptr),
      lbl_trip_dist(nullptr),
      lbl_trip_avg(nullptr),
      lbl_trip_odo(nullptr),
      lbl_trip_fuel(nullptr),
      lbl_gps_sats_2(nullptr),
      lbl_telem_volt(nullptr),
      lbl_telem_volt_sub(nullptr),
      lbl_telem_clt(nullptr),
      lbl_telem_clt_sub(nullptr),
      lbl_telem_afr(nullptr),
      lbl_telem_afr_sub(nullptr),
      lbl_telem_tps(nullptr),
      lbl_telem_tps_sub(nullptr),
      lbl_telem_boost_txt(nullptr),
      bar_telem_boost(nullptr),
      lbl_gps_sats_3(nullptr),
      lbl_m_rpm(nullptr),
      bar_m_shift(nullptr),
      lbl_m_boost(nullptr),
      lbl_m_adv(nullptr),
      lbl_m_offline(nullptr),
      lbl_gps_sats_4(nullptr),
      lbl_set_ble(nullptr),
      lbl_set_can(nullptr),
      lbl_set_gps(nullptr),
      lbl_set_sys(nullptr) {
}

// -----------------------------------------------------------------------------
// Callback сброса буфера кадра LVGL на дисплей ST7789 через SPI 40 МГц
// -----------------------------------------------------------------------------
void UiEngine::dispFlushCb(lv_disp_drv_t* disp_drv, const lv_area_t* area, lv_color_t* color_p) {
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

// Вспомогательная функция создания индикатора спутников в правом верхнем углу
static lv_obj_t* createGpsCornerLabel(lv_obj_t* parent) {
    lv_obj_t* lbl = lv_label_create(parent);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(lbl, COLOR_LV_MID_GRAY, 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_RIGHT, -12, 10);
    lv_label_set_text(lbl, LV_SYMBOL_GPS " --");
    return lbl;
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

    // Создание всех экранов интерфейса
    createScreenClock();
    createScreenTrip();
    createScreenTelem();
    createScreenMPerf();
    createScreenSettings();

    // Загрузка стартового экрана часов
    lv_scr_load(scr_clock);
    currentScreen = ScreenId::CLASSIC_CLOCK;
    lastTickMillis = millis();
}

// -----------------------------------------------------------------------------
// ЭКРАН 1: ЧАСЫ И ТЕЛЕМЕТРИЯ (CLASSIC CLOCK)
// -----------------------------------------------------------------------------
void UiEngine::createScreenClock() {
    scr_clock = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_clock, COLOR_LV_BLACK, 0);
    lv_obj_clear_flag(scr_clock, LV_OBJ_FLAG_SCROLLABLE);

    // Крупные белые часы (нативный сглаженный 4bpp шрифт 51 px)
    lbl_clock = lv_label_create(scr_clock);
    lv_obj_set_style_text_font(lbl_clock, &lv_font_clock_70, 0);
    lv_obj_set_style_text_color(lbl_clock, COLOR_LV_WHITE, 0);
    lv_obj_align(lbl_clock, LV_ALIGN_TOP_MID, 0, 44);
    lv_label_set_text(lbl_clock, "12:34");

    // Индикатор спутников в правом верхнем углу
    lbl_gps_sats_1 = createGpsCornerLabel(scr_clock);

    // Разделительная линия
    createDivider(scr_clock, 15, 126, 290, 1);

    // Колонка 1: BOOST
    lv_obj_t* t_boost = lv_label_create(scr_clock);
    lv_obj_set_style_text_font(t_boost, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(t_boost, COLOR_LV_SILVER, 0);
    lv_obj_set_pos(t_boost, 18, 142);
    lv_label_set_text(t_boost, "BOOST");

    lbl_boost_val = lv_label_create(scr_clock);
    lv_obj_set_style_text_font(lbl_boost_val, &lv_font_telemetry_24, 0);
    lv_obj_set_style_text_color(lbl_boost_val, COLOR_LV_WHITE, 0);
    lv_obj_set_pos(lbl_boost_val, 16, 172);
    lv_label_set_text(lbl_boost_val, "0.00b");

    // Колонка 2: COOLANT
    lv_obj_t* t_clt = lv_label_create(scr_clock);
    lv_obj_set_style_text_font(t_clt, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(t_clt, COLOR_LV_SILVER, 0);
    lv_obj_set_pos(t_clt, 120, 142);
    lv_label_set_text(t_clt, "COOLANT");

    lbl_clt_val = lv_label_create(scr_clock);
    lv_obj_set_style_text_font(lbl_clt_val, &lv_font_telemetry_24, 0);
    lv_obj_set_style_text_color(lbl_clt_val, COLOR_LV_WHITE, 0);
    lv_obj_set_pos(lbl_clt_val, 120, 172);
    lv_label_set_text(lbl_clt_val, "20C");

    // Колонка 3: AFR
    lv_obj_t* t_afr = lv_label_create(scr_clock);
    lv_obj_set_style_text_font(t_afr, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(t_afr, COLOR_LV_SILVER, 0);
    lv_obj_set_pos(t_afr, 230, 142);
    lv_label_set_text(t_afr, "AFR");

    lbl_afr_val = lv_label_create(scr_clock);
    lv_obj_set_style_text_font(lbl_afr_val, &lv_font_telemetry_24, 0);
    lv_obj_set_style_text_color(lbl_afr_val, COLOR_LV_WHITE, 0);
    lv_obj_set_pos(lbl_afr_val, 230, 172);
    lv_label_set_text(lbl_afr_val, "--.-");
}

// -----------------------------------------------------------------------------
// ЭКРАН 2: БОРТОВОЙ КОМПЬЮТЕР И РАСХОД (OBC TRIP & FUEL)
// -----------------------------------------------------------------------------
void UiEngine::createScreenTrip() {
    scr_trip = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_trip, COLOR_LV_BLACK, 0);
    lv_obj_clear_flag(scr_trip, LV_OBJ_FLAG_SCROLLABLE);

    // Спидометр GPS
    lbl_trip_spd = lv_label_create(scr_trip);
    lv_obj_set_style_text_font(lbl_trip_spd, &lv_font_clock_70, 0);
    lv_obj_set_style_text_color(lbl_trip_spd, COLOR_LV_WHITE, 0);
    lv_obj_set_pos(lbl_trip_spd, 16, 14);
    lv_label_set_text(lbl_trip_spd, "  0");

    lv_obj_t* t_kmh = lv_label_create(scr_trip);
    lv_obj_set_style_text_font(t_kmh, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(t_kmh, COLOR_LV_SILVER, 0);
    lv_obj_set_pos(t_kmh, 136, 26);
    lv_label_set_text(t_kmh, "KM/H");

    lbl_trip_fix = lv_label_create(scr_trip);
    lv_obj_set_style_text_font(lbl_trip_fix, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_trip_fix, COLOR_LV_MID_GRAY, 0);
    lv_obj_set_pos(lbl_trip_fix, 136, 48);
    lv_label_set_text(lbl_trip_fix, "NO FIX");

    // Мгновенный расход
    lbl_trip_inst = lv_label_create(scr_trip);
    lv_obj_set_style_text_font(lbl_trip_inst, &lv_font_telemetry_24, 0);
    lv_obj_set_style_text_color(lbl_trip_inst, COLOR_LV_WHITE, 0);
    lv_obj_set_pos(lbl_trip_inst, 185, 36);
    lv_label_set_text(lbl_trip_inst, "0.0 L/H");

    lbl_gps_sats_2 = createGpsCornerLabel(scr_trip);

    // Сетка разделителей
    createDivider(scr_trip, 10, 84, 300, 1);
    createDivider(scr_trip, 10, 160, 300, 1);
    createDivider(scr_trip, 158, 88, 1, 148);

    // Квадрант 1: Суточный пробег
    lv_obj_t* t_dist = lv_label_create(scr_trip);
    lv_obj_set_style_text_font(t_dist, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(t_dist, COLOR_LV_SILVER, 0);
    lv_obj_set_pos(t_dist, 16, 96);
    lv_label_set_text(t_dist, "TRIP DISTANCE:");

    lbl_trip_dist = lv_label_create(scr_trip);
    lv_obj_set_style_text_font(lbl_trip_dist, &lv_font_telemetry_24, 0);
    lv_obj_set_style_text_color(lbl_trip_dist, COLOR_LV_WHITE, 0);
    lv_obj_set_pos(lbl_trip_dist, 16, 120);
    lv_label_set_text(lbl_trip_dist, "0.0 km");

    // Квадрант 2: Средний расход
    lv_obj_t* t_avg = lv_label_create(scr_trip);
    lv_obj_set_style_text_font(t_avg, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(t_avg, COLOR_LV_SILVER, 0);
    lv_obj_set_pos(t_avg, 168, 96);
    lv_label_set_text(t_avg, "AVG CONSUMPTION:");

    lbl_trip_avg = lv_label_create(scr_trip);
    lv_obj_set_style_text_font(lbl_trip_avg, &lv_font_telemetry_24, 0);
    lv_obj_set_style_text_color(lbl_trip_avg, COLOR_LV_WHITE, 0);
    lv_obj_set_pos(lbl_trip_avg, 168, 120);
    lv_label_set_text(lbl_trip_avg, "--- L");

    // Квадрант 3: Общий одометр
    lv_obj_t* t_odo = lv_label_create(scr_trip);
    lv_obj_set_style_text_font(t_odo, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(t_odo, COLOR_LV_SILVER, 0);
    lv_obj_set_pos(t_odo, 16, 170);
    lv_label_set_text(t_odo, "TOTAL ODOMETER:");

    lbl_trip_odo = lv_label_create(scr_trip);
    lv_obj_set_style_text_font(lbl_trip_odo, &lv_font_telemetry_24, 0);
    lv_obj_set_style_text_color(lbl_trip_odo, COLOR_LV_WHITE, 0);
    lv_obj_set_pos(lbl_trip_odo, 16, 194);
    lv_label_set_text(lbl_trip_odo, "0 km");

    // Квадрант 4: Топливо поездки
    lv_obj_t* t_fuel = lv_label_create(scr_trip);
    lv_obj_set_style_text_font(t_fuel, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(t_fuel, COLOR_LV_SILVER, 0);
    lv_obj_set_pos(t_fuel, 168, 170);
    lv_label_set_text(t_fuel, "TRIP FUEL:");

    lbl_trip_fuel = lv_label_create(scr_trip);
    lv_obj_set_style_text_font(lbl_trip_fuel, &lv_font_telemetry_24, 0);
    lv_obj_set_style_text_color(lbl_trip_fuel, COLOR_LV_WHITE, 0);
    lv_obj_set_pos(lbl_trip_fuel, 168, 194);
    lv_label_set_text(lbl_trip_fuel, "0.0 L");
}

// -----------------------------------------------------------------------------
// ЭКРАН 3: ТЕЛЕМЕТРИЯ МОТОРА (OBC TELEMETRY)
// -----------------------------------------------------------------------------
void UiEngine::createScreenTelem() {
    scr_telem = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_telem, COLOR_LV_BLACK, 0);
    lv_obj_clear_flag(scr_telem, LV_OBJ_FLAG_SCROLLABLE);

    lbl_gps_sats_3 = createGpsCornerLabel(scr_telem);

    // Разделители сетки
    createDivider(scr_telem, 10, 84, 300, 1);
    createDivider(scr_telem, 10, 164, 300, 1);
    createDivider(scr_telem, 158, 10, 1, 150);

    // 1. АКБ (верх-лево)
    lv_obj_t* t_v = lv_label_create(scr_telem);
    lv_obj_set_style_text_font(t_v, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(t_v, COLOR_LV_SILVER, 0);
    lv_obj_set_pos(t_v, 16, 12);
    lv_label_set_text(t_v, "BATTERY");

    lbl_telem_volt = lv_label_create(scr_telem);
    lv_obj_set_style_text_font(lbl_telem_volt, &lv_font_telemetry_24, 0);
    lv_obj_set_style_text_color(lbl_telem_volt, COLOR_LV_WHITE, 0);
    lv_obj_set_pos(lbl_telem_volt, 16, 32);
    lv_label_set_text(lbl_telem_volt, "12.4V");

    lbl_telem_volt_sub = lv_label_create(scr_telem);
    lv_obj_set_style_text_font(lbl_telem_volt_sub, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_telem_volt_sub, COLOR_LV_SILVER, 0);
    lv_obj_set_pos(lbl_telem_volt_sub, 16, 62);
    lv_label_set_text(lbl_telem_volt_sub, "IGNITION ON");

    // 2. ОЖ (верх-право)
    lv_obj_t* t_c = lv_label_create(scr_telem);
    lv_obj_set_style_text_font(t_c, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(t_c, COLOR_LV_SILVER, 0);
    lv_obj_set_pos(t_c, 168, 12);
    lv_label_set_text(t_c, "COOLANT");

    lbl_telem_clt = lv_label_create(scr_telem);
    lv_obj_set_style_text_font(lbl_telem_clt, &lv_font_telemetry_24, 0);
    lv_obj_set_style_text_color(lbl_telem_clt, COLOR_LV_WHITE, 0);
    lv_obj_set_pos(lbl_telem_clt, 168, 32);
    lv_label_set_text(lbl_telem_clt, "+25 C");

    lbl_telem_clt_sub = lv_label_create(scr_telem);
    lv_obj_set_style_text_font(lbl_telem_clt_sub, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_telem_clt_sub, COLOR_LV_SILVER, 0);
    lv_obj_set_pos(lbl_telem_clt_sub, 168, 62);
    lv_label_set_text(lbl_telem_clt_sub, "INTAKE: +20 C");

    // 3. AFR (низ-лево)
    lv_obj_t* t_a = lv_label_create(scr_telem);
    lv_obj_set_style_text_font(t_a, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(t_a, COLOR_LV_SILVER, 0);
    lv_obj_set_pos(t_a, 16, 94);
    lv_label_set_text(t_a, "AIR / FUEL (AFR)");

    lbl_telem_afr = lv_label_create(scr_telem);
    lv_obj_set_style_text_font(lbl_telem_afr, &lv_font_telemetry_24, 0);
    lv_obj_set_style_text_color(lbl_telem_afr, COLOR_LV_WHITE, 0);
    lv_obj_set_pos(lbl_telem_afr, 16, 114);
    lv_label_set_text(lbl_telem_afr, "--.-");

    lbl_telem_afr_sub = lv_label_create(scr_telem);
    lv_obj_set_style_text_font(lbl_telem_afr_sub, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_telem_afr_sub, COLOR_LV_SILVER, 0);
    lv_obj_set_pos(lbl_telem_afr_sub, 16, 144);
    lv_label_set_text(lbl_telem_afr_sub, "TARGET: --.-");

    // 4. Дроссель TPS (низ-право)
    lv_obj_t* t_t = lv_label_create(scr_telem);
    lv_obj_set_style_text_font(t_t, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(t_t, COLOR_LV_SILVER, 0);
    lv_obj_set_pos(t_t, 168, 94);
    lv_label_set_text(t_t, "THROTTLE (TPS)");

    lbl_telem_tps = lv_label_create(scr_telem);
    lv_obj_set_style_text_font(lbl_telem_tps, &lv_font_telemetry_24, 0);
    lv_obj_set_style_text_color(lbl_telem_tps, COLOR_LV_WHITE, 0);
    lv_obj_set_pos(lbl_telem_tps, 168, 114);
    lv_label_set_text(lbl_telem_tps, "0%");

    lbl_telem_tps_sub = lv_label_create(scr_telem);
    lv_obj_set_style_text_font(lbl_telem_tps_sub, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_telem_tps_sub, COLOR_LV_SILVER, 0);
    lv_obj_set_pos(lbl_telem_tps_sub, 168, 144);
    lv_label_set_text(lbl_telem_tps_sub, "MAP: 100 kPa");

    // Нижняя шкала наддува
    lbl_telem_boost_txt = lv_label_create(scr_telem);
    lv_obj_set_style_text_font(lbl_telem_boost_txt, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_telem_boost_txt, COLOR_LV_SILVER, 0);
    lv_obj_set_pos(lbl_telem_boost_txt, 18, 175);
    lv_label_set_text(lbl_telem_boost_txt, "BOOST: 0.00 Bar (100 kPa)");

    bar_telem_boost = lv_bar_create(scr_telem);
    lv_obj_set_pos(bar_telem_boost, 18, 198);
    lv_obj_set_size(bar_telem_boost, 284, 18);
    lv_bar_set_range(bar_telem_boost, 0, 230); // -0.8 .. +1.5 bar
    lv_bar_set_value(bar_telem_boost, 80, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar_telem_boost, COLOR_LV_DARK_GRAY, 0);
    lv_obj_set_style_bg_color(bar_telem_boost, COLOR_LV_WHITE, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar_telem_boost, 2, 0);
    lv_obj_set_style_radius(bar_telem_boost, 2, LV_PART_INDICATOR);
}

// -----------------------------------------------------------------------------
// ЭКРАН 4: ///M PERFORMANCE & ТАХОМЕТР
// -----------------------------------------------------------------------------
void UiEngine::createScreenMPerf() {
    scr_m_perf = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_m_perf, COLOR_LV_BLACK, 0);
    lv_obj_clear_flag(scr_m_perf, LV_OBJ_FLAG_SCROLLABLE);

    lbl_gps_sats_4 = createGpsCornerLabel(scr_m_perf);

    // RPM обороты
    lbl_m_rpm = lv_label_create(scr_m_perf);
    lv_obj_set_style_text_font(lbl_m_rpm, &lv_font_clock_70, 0);
    lv_obj_set_style_text_color(lbl_m_rpm, COLOR_LV_WHITE, 0);
    lv_obj_set_pos(lbl_m_rpm, 20, 16);
    lv_label_set_text(lbl_m_rpm, "0");

    lv_obj_t* t_rpm = lv_label_create(scr_m_perf);
    lv_obj_set_style_text_font(t_rpm, &lv_font_telemetry_24, 0);
    lv_obj_set_style_text_color(t_rpm, COLOR_LV_WHITE, 0);
    lv_obj_set_pos(t_rpm, 185, 42);
    lv_label_set_text(t_rpm, "RPM");

    // Прогрессивная полоса тахометра
    bar_m_shift = lv_bar_create(scr_m_perf);
    lv_obj_set_pos(bar_m_shift, 18, 88);
    lv_obj_set_size(bar_m_shift, 284, 24);
    lv_bar_set_range(bar_m_shift, 0, 7500);
    lv_bar_set_value(bar_m_shift, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar_m_shift, COLOR_LV_DARK_GRAY, 0);
    lv_obj_set_style_bg_color(bar_m_shift, COLOR_LV_WHITE, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar_m_shift, 2, 0);
    lv_obj_set_style_radius(bar_m_shift, 2, LV_PART_INDICATOR);

    // Метки шкалы
    lv_obj_t* t_scale = lv_label_create(scr_m_perf);
    lv_obj_set_style_text_font(t_scale, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(t_scale, COLOR_LV_SILVER, 0);
    lv_obj_set_pos(t_scale, 20, 118);
    lv_label_set_text(t_scale, "0         2k        4k        6k       7.5k");

    createDivider(scr_m_perf, 15, 142, 290, 1);

    // Нижние спортивные показатели
    lv_obj_t* t_mb = lv_label_create(scr_m_perf);
    lv_obj_set_style_text_font(t_mb, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(t_mb, COLOR_LV_SILVER, 0);
    lv_obj_set_pos(t_mb, 20, 154);
    lv_label_set_text(t_mb, "BOOST");

    lbl_m_boost = lv_label_create(scr_m_perf);
    lv_obj_set_style_text_font(lbl_m_boost, &lv_font_telemetry_24, 0);
    lv_obj_set_style_text_color(lbl_m_boost, COLOR_LV_WHITE, 0);
    lv_obj_set_pos(lbl_m_boost, 20, 184);
    lv_label_set_text(lbl_m_boost, "0.00b");

    lv_obj_t* t_ma = lv_label_create(scr_m_perf);
    lv_obj_set_style_text_font(t_ma, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(t_ma, COLOR_LV_SILVER, 0);
    lv_obj_set_pos(t_ma, 168, 154);
    lv_label_set_text(t_ma, "ADVANCE");

    lbl_m_adv = lv_label_create(scr_m_perf);
    lv_obj_set_style_text_font(lbl_m_adv, &lv_font_telemetry_24, 0);
    lv_obj_set_style_text_color(lbl_m_adv, COLOR_LV_WHITE, 0);
    lv_obj_set_pos(lbl_m_adv, 168, 184);
    lv_label_set_text(lbl_m_adv, "10.0*");

    // Статус оффлайн
    lbl_m_offline = lv_label_create(scr_m_perf);
    lv_obj_set_style_text_font(lbl_m_offline, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_m_offline, COLOR_LV_MID_GRAY, 0);
    lv_obj_align(lbl_m_offline, LV_ALIGN_CENTER, 0, -40);
    lv_label_set_text(lbl_m_offline, "WAITING MS2 CAN...");
    lv_obj_add_flag(lbl_m_offline, LV_OBJ_FLAG_HIDDEN);
}

// -----------------------------------------------------------------------------
// ЭКРАН 5: НАСТРОЙКИ И ИНФО (SETTINGS & INFO)
// -----------------------------------------------------------------------------
void UiEngine::createScreenSettings() {
    scr_settings = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_settings, COLOR_LV_BLACK, 0);
    lv_obj_clear_flag(scr_settings, LV_OBJ_FLAG_SCROLLABLE);

    createDivider(scr_settings, 15, 62, 290, 1);
    createDivider(scr_settings, 15, 136, 290, 1);
    createDivider(scr_settings, 15, 200, 290, 1);

    // Секция 1: Bluetooth
    lbl_set_ble = lv_label_create(scr_settings);
    lv_obj_set_style_text_font(lbl_set_ble, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_set_ble, COLOR_LV_WHITE, 0);
    lv_obj_set_pos(lbl_set_ble, 20, 24);
    lv_label_set_text(lbl_set_ble, LV_SYMBOL_BLUETOOTH " BLE: BMW_OBC_E36 (READY)");

    // Секция 2: CAN-шина MegaSquirt 2
    lbl_set_can = lv_label_create(scr_settings);
    lv_obj_set_style_text_font(lbl_set_can, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_set_can, COLOR_LV_WHITE, 0);
    lv_obj_set_pos(lbl_set_can, 20, 88);
    lv_label_set_text(lbl_set_can, "CAN-BUS: 500 kbps (OFFLINE)");

    // Секция 3: GPS модуль
    lbl_set_gps = lv_label_create(scr_settings);
    lv_obj_set_style_text_font(lbl_set_gps, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_set_gps, COLOR_LV_WHITE, 0);
    lv_obj_set_pos(lbl_set_gps, 20, 156);
    lv_label_set_text(lbl_set_gps, LV_SYMBOL_GPS " GPS: NEO-7M (NO FIX)");

    // Секция 4: Версия прошивки
    lbl_set_sys = lv_label_create(scr_settings);
    lv_obj_set_style_text_font(lbl_set_sys, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_set_sys, COLOR_LV_SILVER, 0);
    lv_obj_set_pos(lbl_set_sys, 20, 214);
    lv_label_set_text(lbl_set_sys, "BMW E36 OBC v2.4 (LVGL 8.3) - ESP32-S3");
}

void UiEngine::showBootSplash(const char* subtitle) {
    Display.setBrightness(0);
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

    Display.fadeIn(DEFAULT_BRIGHTNESS, 4);
    delay(900);
    Display.fadeOut(3);

    tft.fillScreen(0x0000);
    setScreen(ScreenId::CLASSIC_CLOCK);
    Display.fadeIn(DEFAULT_BRIGHTNESS, 3);
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
            case ScreenId::SETTINGS_INFO:  target = scr_settings; break;
            default: target = scr_clock; break;
        }
        if (target) {
            lv_scr_load(target);
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

    // Наддув (BOOST)
    if (sens.ms2Online) {
        lv_label_set_text_fmt(lbl_boost_val, "%+.2fb", sens.ms2.boost_bar);
    } else {
        lv_label_set_text(lbl_boost_val, "0.00b");
    }

    // Температура ОЖ (COOLANT)
    if (sens.ms2Online) {
        lv_label_set_text_fmt(lbl_clt_val, "%dC", (int)round(sens.ms2.clt_c));
    } else {
        lv_label_set_text_fmt(lbl_clt_val, "%dC", (int)round(sens.tempOutdoor));
    }

    // Смесь (AFR)
    if (sens.ms2Online) {
        lv_label_set_text_fmt(lbl_afr_val, "%.1f", sens.ms2.afr);
    } else {
        lv_label_set_text(lbl_afr_val, "--.-");
    }
}

void UiEngine::updateTripScreen() {
    const TripData& trip = Trip.getData();
    const GpsData& gps = Gps.getData();

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

    // 4 Квадранта
    lv_label_set_text_fmt(lbl_trip_dist, "%.1f km", trip.trip_distance_km);
    if (trip.trip_distance_km >= 0.1f) {
        lv_label_set_text_fmt(lbl_trip_avg, "%.1f L", trip.avg_consumption_l_100km);
    } else {
        lv_label_set_text(lbl_trip_avg, "--- L");
    }
    lv_label_set_text_fmt(lbl_trip_odo, "%.0f km", trip.total_odometer_km);
    lv_label_set_text_fmt(lbl_trip_fuel, "%.1f L", trip.trip_fuel_liters);
}

void UiEngine::updateTelemScreen() {
    const SensorData& sens = Sensors.getData();
    const GpsData& gps = Gps.getData();

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
    lv_label_set_text_fmt(lbl_telem_clt, "%+.0f C", sens.tempOutdoor);
    lv_label_set_text_fmt(lbl_telem_clt_sub, "INTAKE: %+.0f C", sens.tempCabin);

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
