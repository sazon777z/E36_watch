#include "web_portal.h"
#include "time_keeper.h"
#include "sensors.h"
#include "ui_engine.h"
#include "display_driver.h"
#include <WiFiClient.h>

WebPortal Portal;

WebPortal::WebPortal() : server(80), wifiConnected(false) {
    stationIp[0] = '\0';
}

void WebPortal::init() {
    // Поднимаем точку доступа Wi-Fi (AP)
    WiFi.mode(WIFI_AP_STA);
    IPAddress apIP(192, 168, AP_IP_OCTET, 1);
    IPAddress netMsk(255, 255, 255, 0);
    WiFi.softAPConfig(apIP, apIP, netMsk);
    WiFi.softAP(AP_SSID, AP_PASSWORD);

    setupRoutes();
    server.begin();
}

void WebPortal::update() {
    server.handleClient();
}

void WebPortal::setupRoutes() {
    server.on("/", HTTP_GET, [this]() { handleRoot(); });
    server.on("/save_wifi", HTTP_POST, [this]() { handleSaveSettings(); });
    server.on("/set_time", HTTP_POST, [this]() { handleSetTime(); });
    server.on("/set_brightness", HTTP_POST, [this]() { handleSetBrightness(); });
    server.on("/next_screen", HTTP_POST, [this]() { handleNextScreen(); });
}

void WebPortal::handleRoot() {
    const TimeData& td = Time.getTime();
    const SensorData& sens = Sensors.getData();

    String html = "<!DOCTYPE html><html><head><meta charset='utf-8'>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<title>BMW E36 OBC Control</title>";
    html += "<style>";
    html += "body{background:#111;color:#eee;font-family:-apple-system,BlinkMacSystemFont,Segoe UI,Roboto,sans-serif;margin:0;padding:15px;}";
    html += ".container{max-width:480px;margin:0 auto;}";
    html += ".card{background:#1e1e1e;border:1px solid #333;border-left:4px solid #ff7f00;padding:15px;margin-bottom:15px;border-radius:6px;}";
    html += "h1,h2{color:#ff7f00;margin-top:0;}";
    html += ".val{color:#fff;font-weight:bold;font-size:1.2em;}";
    html += "input,select,button{width:100%;padding:10px;margin:8px 0;box-sizing:border-box;border-radius:4px;border:1px solid #444;background:#2a2a2a;color:#fff;}";
    html += "button{background:#ff7f00;color:#000;font-weight:bold;border:none;cursor:pointer;padding:12px;margin-top:12px;}";
    html += "button:hover{background:#ffa033;}";
    html += ".btn-sec{background:#333;color:#ff7f00;border:1px solid #ff7f00;}";
    html += ".grid{display:grid;grid-template-columns:1fr 1fr;gap:10px;}";
    html += "</style></head><body><div class='container'>";
    
    html += "<h1>/// BMW E36 OBC</h1>";

    // Карточка телеметрии
    html += "<div class='card'>";
    html += "<h2>Телеметрия E36</h2>";
    html += "<div class='grid'>";
    html += "<div>Время: <div class='val'>" + String(td.timeStr) + "</div></div>";
    html += "<div>Дата: <div class='val'>" + String(td.dateStr) + "</div></div>";
    html += "<div>АКБ: <div class='val'>" + String(sens.batteryVoltage, 2) + " V</div></div>";
    html += "<div>Улица: <div class='val'>" + String(sens.tempOutdoor, 1) + " &deg;C</div></div>";
    html += "</div>";
    html += "<form action='/next_screen' method='POST'><button class='btn-sec' type='submit'>Сменить экран на дисплее</button></form>";
    html += "</div>";

    // Управление подсветкой
    html += "<div class='card'>";
    html += "<h2>Яркость дисплея</h2>";
    html += "<form action='/set_brightness' method='POST'>";
    html += "<input type='range' name='brightness' min='10' max='255' value='" + String(Display.getBrightness()) + "' oninput='this.nextElementSibling.value = this.value'>";
    html += "<output style='color:#ff7f00;font-weight:bold;'>" + String(Display.getBrightness()) + "</output> / 255";
    html += "<button type='submit'>Применить яркость</button>";
    html += "</form></div>";

    // Подключение к Wi-Fi для авто-синхронизации NTP
    html += "<div class='card'>";
    html += "<h2>Wi-Fi (Интернет / NTP)</h2>";
    html += "<form action='/save_wifi' method='POST'>";
    html += "<label>Имя сети (SSID):</label>";
    html += "<input type='text' name='ssid' placeholder='Раздача с телефона / Домашняя сеть'>";
    html += "<label>Пароль:</label>";
    html += "<input type='password' name='pass' placeholder='Пароль от Wi-Fi'>";
    html += "<label>Часовой пояс (UTC):</label>";
    html += "<select name='tz'>";
    html += "<option value='2'>UTC+2 (Калининград, Киев, Кишинев)</option>";
    html += "<option value='3' selected>UTC+3 (Москва, СПб, Минск)</option>";
    html += "<option value='4'>UTC+4 (Самара, Баку, Тбилиси)</option>";
    html += "<option value='5'>UTC+5 (Екатеринбург, Ташкент)</option>";
    html += "<option value='6'>UTC+6 (Омск, Астана)</option>";
    html += "<option value='7'>UTC+7 (Новосибирск, Красноярск)</option>";
    html += "</select>";
    html += "<button type='submit'>Подключить и синхронизировать</button>";
    html += "</form></div>";

    // Ручная установка времени
    html += "<div class='card'>";
    html += "<h2>Ручная установка времени</h2>";
    html += "<form action='/set_time' method='POST'>";
    html += "<label>Время (ЧЧ:ММ:СС):</label>";
    html += "<input type='time' name='time' step='1' value='" + String(td.timeStr) + "'>";
    html += "<label>Дата:</label>";
    html += "<input type='date' name='date' value='" + String(td.year) + "-" + 
            (td.month < 10 ? "0" : "") + String(td.month) + "-" + 
            (td.day < 10 ? "0" : "") + String(td.day) + "'>";
    html += "<button type='submit'>Установить вручную</button>";
    html += "</form></div>";

    html += "<div style='text-align:center;color:#666;font-size:0.85em;'>BMW E36 OBC System &bull; 1991</div>";
    html += "</div></body></html>";

    server.send(200, "text/html", html);
}

void WebPortal::handleSaveSettings() {
    if (server.hasArg("ssid") && server.hasArg("pass")) {
        String ssid = server.arg("ssid");
        String pass = server.arg("pass");
        int tz = server.hasArg("tz") ? server.arg("tz").toInt() : DEFAULT_TIMEZONE_OFFSET;

        if (ssid.length() > 0) {
            WiFi.begin(ssid.c_str(), pass.c_str());
            // Пытаемся синхронизировать NTP после подключения
            unsigned long startWait = millis();
            while (WiFi.status() != WL_CONNECTED && millis() - startWait < 5000) {
                delay(100);
            }
            if (WiFi.status() == WL_CONNECTED) {
                wifiConnected = true;
                strncpy(stationIp, WiFi.localIP().toString().c_str(), sizeof(stationIp));
                Time.syncNtp(tz, 0);
            }
        }
    }
    server.sendHeader("Location", "/");
    server.send(303);
}

void WebPortal::handleSetTime() {
    if (server.hasArg("time") && server.hasArg("date")) {
        String timeStr = server.arg("time"); // "HH:MM:SS" or "HH:MM"
        String dateStr = server.arg("date"); // "YYYY-MM-DD"

        int hour = 0, min = 0, sec = 0;
        int year = 2026, month = 1, day = 1;

        sscanf(timeStr.c_str(), "%d:%d:%d", &hour, &min, &sec);
        sscanf(dateStr.c_str(), "%d-%d-%d", &year, &month, &day);

        Time.setManualTime(year, month, day, hour, min, sec);
    }
    server.sendHeader("Location", "/");
    server.send(303);
}

void WebPortal::handleSetBrightness() {
    if (server.hasArg("brightness")) {
        int b = server.arg("brightness").toInt();
        Display.setBrightness(constrain(b, 0, 255));
    }
    server.sendHeader("Location", "/");
    server.send(303);
}

void WebPortal::handleNextScreen() {
    UI.nextScreen();
    server.sendHeader("Location", "/");
    server.send(303);
}
