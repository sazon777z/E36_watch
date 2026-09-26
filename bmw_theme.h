#ifndef BMW_THEME_H
#define BMW_THEME_H

#include <Arduino.h>

// =============================================================================
// ЦВЕТОВАЯ ПАЛИТРА BMW E36 (RGB565)
// =============================================================================

// Лаконичная монохромная палитра (Белый жирный на черном)
#define COLOR_BLACK             0x0000  // Глубокий абсолютно черный фон
#define COLOR_WHITE             0xFFFF  // Чистый белый (основные цифры и текст)
#define COLOR_SILVER            0xC618  // Благородный серебристый (подписи, единицы измерения)
#define COLOR_MID_GRAY          0x8410  // Нейтральный серый (второстепенные данные)
#define COLOR_DARK_GRAY         0x2104  // Тонкие разделительные линии (1px)
#define COLOR_GHOST_SEG         0x1082  // Едва заметная тень выключенных сегментов

// Фирменный янтарно-оранжевый BMW Amber (сохранен для совместимости)
#define COLOR_BMW_AMBER_BRIGHT  0xFD20  // Яркий янтарный
#define COLOR_BMW_AMBER_MAIN    0xFB40  // Основной янтарный цвет E36
#define COLOR_BMW_AMBER_DIM     0x99C0  // Приглушенный янтарный
#define COLOR_BMW_AMBER_GRID    0x1082  // Теневая сетка знакомест
#define COLOR_BMW_AMBER_DARK    0x1082  // Фон знакомест
#define COLOR_LIGHT_GRAY        0xC618  // Светло-серый

// Спортивная палитра BMW M-Power (Триколор M-Tech)
#define COLOR_BMW_M_CYAN        0x04DC  // Голубой (M Светло-синий)
#define COLOR_BMW_M_BLUE        0x018B  // Темно-синий (M Синий)
#define COLOR_BMW_M_RED         0xD800  // Красный (M Красный)

// Статусные цвета индикаторов
#define COLOR_STATUS_OK         0x0740  // Зеленый (Норма)
#define COLOR_STATUS_WARN       0xFD00  // Желтый (Предупреждение)
#define COLOR_STATUS_ERR        0xF800  // Красный (Критическая ошибка/просадка АКБ)

// Хелпер конверсии RGB888 в RGB565
inline constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

#endif // BMW_THEME_H
