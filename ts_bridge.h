#ifndef TS_BRIDGE_H
#define TS_BRIDGE_H

// =============================================================================
// Беспроводной мост TunerStudio <-> MegaSquirt 2
//   ЭБУ (FT232RL, USB) --кабель--> USB дэшборда (ESP32-S3 как USB-хост)
//   дэшборд <~~ ESP-NOW ~~> донгл ESP32-C3 в USB ноутбука (постоянный COM-порт)
//
// Питание FT232 (VBUS кабеля к ЭБУ) подается ключом PIN_USB_VBUS_EN только пока
// MegaSquirt online по CAN, и снимается перед сном: иначе FT232 на стоянке
// подпитывал бы выключенный ЭБУ через свою линию TX.
//
// СЕРВИСНЫЙ РЕЖИМ: держите кнопку (GPIO 14) при включении питания (не при
// пробуждении) ~1.5 с — мост не запустится, USB платы работает как раньше
// (монитор порта, команды, прошивка из Arduino IDE).
// =============================================================================

#include <Arduino.h>
#include "config.h"

class TsBridgeClass {
public:
    void init();       // после BleMgr.init()
    void update();     // из loop(): управление питанием FT232
    void shutdown();   // перед глубоким сном

    bool isRunning() const { return running; }
    bool ecuReady() const;    // FT232 ЭБУ найден и настроен
    bool linkUp() const;      // донгл ноутбука на связи
    const char* statusStr() const;

private:
    bool running = false;
    bool vbusOn = false;
    void setVbus(bool on);
};

extern TsBridgeClass TsBridge;

#endif // TS_BRIDGE_H
