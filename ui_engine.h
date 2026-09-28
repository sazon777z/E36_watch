#ifndef UI_ENGINE_H
#define UI_ENGINE_H

#include <Arduino.h>
#include <lvgl.h>
#include "display_driver.h"
#include "time_keeper.h"
#include "sensors.h"
#include "gps_driver.h"
#include "trip_computer.h"
#include "bmw_theme.h"

#include "warning_manager.h"

// Объявления кастомных 4bpp сглаженных шрифтов
extern "C" {
    extern const lv_font_t lv_font_clock_70;
    extern const lv_font_t lv_font_telemetry_24;
}

enum class ScreenId {
    BOOT_SPLASH = 0,
    CLASSIC_CLOCK,       // 1
    OBC_TRIP_FUEL,       // 2
    OBC_TELEMETRY,       // 3
    M_PERFORMANCE,       // 4
    SINGLE_GAUGE,        // 5: Полноэкранный цифровой прибор
    SETTINGS_INFO,       // 6: Настройки и инфо
    COUNT
};

enum class GaugeType {
    BOOST = 0,       // Наддув (Бар)
    COOLANT,         // ОЖ (°C)
    AFR,             // Смесь ШЛЗ
    INST_FUEL,       // Мгновенный расход (Л/100км или Л/ч)
    SPEED,           // Скорость GPS (км/ч)
    INTAKE_TEMP,     // Впускной воздух IAT (°C)
    RPM,             // Тахометр (об/мин)
    COUNT
};

class UiEngine {
public:
    UiEngine();

    void init();
    void update();

    // Переключение экранов
    void nextScreen();
    void setScreen(ScreenId screen);
    ScreenId getCurrentScreen() const { return currentScreen; }

    // Управление полноэкранным аналоговым прибором
    void setGaugeType(GaugeType type);
    void nextGaugeType();
    GaugeType getGaugeType() const { return currentGauge; }

    // Заставка загрузки
    void showBootSplash(const char* subtitle = "ON-BOARD COMPUTER");

    // Визуальное подтверждение сброса пробега
    void notifyTripReset();

    // Заглушки для совместимости
    void toggleStopwatch() {}
    void resetStopwatch() {}

private:
    ScreenId currentScreen;
    ScreenId lastScreen;
    bool colonState;
    unsigned long lastColonBlinkMillis;
    unsigned long lastTickMillis;

    // LVGL дисплейный буфер (320 x 40 строк в SRAM)
    static const uint32_t LV_BUF_LINES = 40;
    lv_disp_draw_buf_t draw_buf;
    lv_color_t buf_1[320 * LV_BUF_LINES];
    lv_disp_drv_t disp_drv;

    // LVGL Экраны
    lv_obj_t* scr_clock;
    lv_obj_t* scr_trip;
    lv_obj_t* scr_telem;
    lv_obj_t* scr_m_perf;
    lv_obj_t* scr_gauge;
    lv_obj_t* scr_settings;

    // Виджеты Экрана 1: Часы и нижняя телеметрия
    lv_obj_t* lbl_clock;
    lv_obj_t* lbl_gps_sats_1;
    lv_obj_t* lbl_ms2_status_1;
    lv_obj_t* card_boost_1;
    lv_obj_t* lbl_boost_val;
    lv_obj_t* lbl_boost_sub;
    lv_obj_t* card_clt_1;
    lv_obj_t* lbl_clt_val;
    lv_obj_t* lbl_clt_sub;
    lv_obj_t* card_afr_1;
    lv_obj_t* lbl_afr_val;
    lv_obj_t* lbl_afr_sub;

    // Виджеты Экрана 2: Бортовой компьютер и расход (3 плитки)
    lv_obj_t* lbl_trip_spd;
    lv_obj_t* lbl_trip_fix;
    lv_obj_t* lbl_trip_inst;
    lv_obj_t* card_trip_dist;
    lv_obj_t* lbl_trip_dist;
    lv_obj_t* lbl_trip_reset_hint;
    lv_obj_t* card_trip_avg;
    lv_obj_t* lbl_trip_avg;
    lv_obj_t* card_trip_fuel;
    lv_obj_t* lbl_trip_fuel;
    lv_obj_t* lbl_gps_sats_2;
    lv_obj_t* lbl_ms2_status_2;

    // Виджеты Экрана 3: Телеметрия мотора
    lv_obj_t* lbl_telem_volt;
    lv_obj_t* lbl_telem_volt_sub;
    lv_obj_t* card_telem_clt;
    lv_obj_t* lbl_telem_clt;
    lv_obj_t* lbl_telem_clt_sub;
    lv_obj_t* card_telem_afr;
    lv_obj_t* lbl_telem_afr;
    lv_obj_t* lbl_telem_afr_sub;
    lv_obj_t* lbl_telem_tps;
    lv_obj_t* lbl_telem_tps_sub;
    lv_obj_t* card_telem_boost;
    lv_obj_t* lbl_telem_boost_txt;
    lv_obj_t* bar_telem_boost;
    lv_obj_t* lbl_gps_sats_3;
    lv_obj_t* lbl_ms2_status_3;

    // Виджеты Экрана 4: ///M Тахометр
    lv_obj_t* lbl_m_rpm;
    lv_obj_t* bar_m_shift;
    lv_obj_t* card_m_boost;
    lv_obj_t* lbl_m_boost;
    lv_obj_t* lbl_m_adv;
    lv_obj_t* lbl_m_offline;
    lv_obj_t* lbl_gps_sats_4;
    lv_obj_t* lbl_ms2_status_4;

    // Виджеты Экрана 5: Полноэкранный цифровой прибор
    GaugeType currentGauge;
    lv_obj_t* card_digital_gauge;
    lv_obj_t* lbl_digital_title;
    lv_obj_t* lbl_digital_val;
    lv_obj_t* lbl_digital_unit;
    lv_obj_t* lbl_digital_warn_limit;

    // Виджеты Экрана 6: Настройки и инфо
    lv_obj_t* lbl_set_ble;
    lv_obj_t* lbl_set_can;
    lv_obj_t* lbl_set_gps;
    lv_obj_t* lbl_set_sys;

    // Создание экранов
    void createScreenClock();
    void createScreenTrip();
    void createScreenTelem();
    void createScreenMPerf();
    void createScreenGauge();
    void createScreenSettings();

    // Обновление данных
    void updateClockScreen();
    void updateTripScreen();
    void updateTelemScreen();
    void updateMPerfScreen();
    void updateGaugeScreen();
    void updateSettingsScreen();

    void loadGaugeFromNvs();
    void saveGaugeToNvs();

    // Callbacks LVGL
    static void dispFlushCb(lv_disp_drv_t* disp_drv, const lv_area_t* area, lv_color_t* color_p);
};

extern UiEngine UI;

#endif // UI_ENGINE_H
