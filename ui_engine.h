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
    extern const lv_font_t lv_font_cyr_12;   // Кириллица 12 px, остальное - из Montserrat 12
}

enum class ScreenId {
    BOOT_SPLASH = 0,
    CLASSIC_CLOCK,       // 1: Часы + BOOST / COOLANT / AFR
    OBC_TRIP_FUEL,       // 2: Скорость, расход, трип
    OBC_TELEMETRY,       // 3: Телеметрия мотора
    M_PERFORMANCE,       // 4: ///M тахометр с шифт-лайтом
    SINGLE_GAUGE,        // 5: Полноэкранный стрелочный прибор
    SETTINGS_INFO,       // 6: Система
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

// Плавное (экспоненциальное) приближение отображаемого значения к реальному
struct SmoothValue {
    float value = 0.0f;
    bool primed = false;
    float step(float target, float dtSec, float tauSec);
    void snap(float target) { value = target; primed = true; }
};

// Метка, которая перерисовывается только при реальном изменении текста
struct CachedLabel {
    lv_obj_t* obj = nullptr;
    char last[40] = {0};
    bool initialized = false;
    void set(const char* text);
    void setf(const char* fmt, ...);
};

// Плитка: заголовок, значение, единица и мини-шкала
struct Tile {
    lv_obj_t* box = nullptr;
    CachedLabel value;
    CachedLabel sub;
    lv_obj_t* bar = nullptr;
    int32_t lastBar = INT32_MIN;
    uintptr_t alarmState = 0;
    void setBar(int32_t v);
    void setAlarm(bool alarm, bool blinkPhase);
};

class UiEngine {
public:
    UiEngine();

    void init();
    void update();   // Вызывается в каждом проходе loop(): данные ~30 Гц, отрисовка до 60 Гц

    // Переключение экранов (с анимацией сдвига)
    void nextScreen();
    void setScreen(ScreenId screen);
    ScreenId getCurrentScreen() const { return currentScreen; }

    // Полноэкранный прибор
    void setGaugeType(GaugeType type);
    void nextGaugeType();
    GaugeType getGaugeType() const { return currentGauge; }

    // Заставка загрузки
    void showBootSplash(const char* subtitle = "ON-BOARD COMPUTER");

    // Визуальное подтверждение сброса пробега
    void notifyTripReset();

    // Снимок экрана в монитор порта (команда 'c'), для отладки дизайна
    void captureScreen();

private:
    ScreenId currentScreen;
    GaugeType currentGauge;
    unsigned long lastDataMs;
    unsigned long tripResetNoticeMs;

    // LVGL дисплейный буфер
    static const uint32_t LV_BUF_LINES = 48;
    lv_disp_draw_buf_t draw_buf;
    lv_color_t buf_1[320 * LV_BUF_LINES];
    lv_disp_drv_t disp_drv;

    lv_obj_t* screens[(int)ScreenId::COUNT];

    // Общий верхний слой (не уезжает при смене экранов)
    lv_obj_t* ms2Dot;
    CachedLabel ms2Text;
    CachedLabel titleLbl;
    CachedLabel gpsLbl;
    lv_obj_t* pageDots[(int)ScreenId::COUNT - 1];

    // Экран 1: часы
    // Часы: часы, двоеточие и минуты - отдельные метки, двоеточие мигает прозрачностью
    // (цифры не сдвигаются, в отличие от замены ':' на пробел)
    CachedLabel clockH;
    CachedLabel clockM;
    lv_obj_t* clockColon;
    int8_t colonShown;
    Tile clkBoost, clkClt, clkAfr;

    // Экран 2: трип
    CachedLabel tripSpeed;
    CachedLabel tripFix;
    CachedLabel tripInst;
    CachedLabel tripInstUnit;
    lv_obj_t* tripInstBar;
    int32_t tripInstBarLast;
    Tile tripDist, tripAvg, tripFuel;

    // Экран 3: мотор
    Tile engVolt, engClt, engAfr, engTps;
    Tile engBoost;

    // Экран 4: ///M
    static const int SHIFT_SEGMENTS = 16;
    lv_obj_t* shiftSeg[SHIFT_SEGMENTS];
    int8_t shiftLit;
    bool shiftFlash;
    CachedLabel mRpm;
    Tile mBoost, mAdv;

    // Экран 5: прибор
    lv_obj_t* gaugeArc;
    int32_t gaugeArcLast;
    CachedLabel gaugeVal;
    CachedLabel gaugeUnit;
    CachedLabel gaugePeak;
    CachedLabel gaugeLimit;
    CachedLabel gaugeMin;
    CachedLabel gaugeMax;
    uintptr_t gaugeAlarmState;

    // Экран 6: система
    CachedLabel infoBle, infoCan, infoGps, infoFw;

    // Сглаженные значения
    SmoothValue sRpm, sBoost, sAfr, sClt, sMat, sTps, sVolt, sAdv, sSpeed, sInst;

    // Построение интерфейса
    void createTopLayer();
    void createScreenClock();
    void createScreenTrip();
    void createScreenTelem();
    void createScreenMPerf();
    void createScreenGauge();
    void createScreenSettings();

    // Обновление данных
    void stepSmoothing(float dt);
    void updateTopLayer();
    void updateClockScreen();
    void updateTripScreen();
    void updateTelemScreen();
    void updateMPerfScreen();
    void updateGaugeScreen(bool force = false);
    void updateSettingsScreen();

    void loadGaugeFromNvs();
    void saveGaugeToNvs();

    static void dispFlushCb(lv_disp_drv_t* disp_drv, const lv_area_t* area, lv_color_t* color_p);
};

extern UiEngine UI;

#endif // UI_ENGINE_H
