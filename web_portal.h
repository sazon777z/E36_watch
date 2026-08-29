#ifndef WEB_PORTAL_H
#define WEB_PORTAL_H

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include "config.h"

class WebPortal {
public:
    WebPortal();

    void init();
    void update();

    bool isConnectedToStation() const { return wifiConnected; }
    const char* getStationIp() const { return stationIp; }

private:
    WebServer server;
    bool wifiConnected;
    char stationIp[16];

    void setupRoutes();
    void handleRoot();
    void handleSaveSettings();
    void handleSetTime();
    void handleSetBrightness();
    void handleNextScreen();
};

extern WebPortal Portal;

#endif // WEB_PORTAL_H
