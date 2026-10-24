#include "logo.h"
#include "flashguard.h"
// ZRadio – internetni radio za ESP32-S3 (N16R8) + PCM5102 + OLED
#include <Arduino.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include "config.h"
#include "stations.h"
#include "player.h"
#include "input.h"
#include "display.h"
#include "app.h"
#include "web.h"
#include "diag.h"

static const char* AP_PASS = "zradio1234";
static uint32_t bootStableAt = 0;
static Preferences sys;

static String apName() {
    uint8_t m[6]; WiFi.macAddress(m);
    char b[16]; snprintf(b, sizeof(b), "ZRadio-%02X%02X", m[4], m[5]);
    return b;
}

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.printf("\nZRadio %s\n", ZR_VERSION);

    // varni način: 3 zaporedni neuspešni zagoni -> brez periferije, samo splet
    sys.begin("sys", false);
    uint8_t fails = sys.getUChar("bootfail", 0) + 1;
    { FlashGuard g("zagon"); sys.putUChar("bootfail", fails); }
    safeMode = fails >= 3;
    if (safeMode) Serial.println("!!! VARNI NAČIN – periferija ni inicializirana");

    // particija se imenuje "littlefs" (privzeto bi knjižnica iskala "spiffs")
    bool fsOk = LittleFS.begin(true, "/littlefs", 10, "littlefs");
    diagBegin();
    if (!fsOk) diagEvent("NAPAKA: datotečnega sistema ni mogoče priklopiti – nastavitve se ne bodo shranile");
    else diagEvent("Datotečni sistem: %u / %u kB", (unsigned)(LittleFS.usedBytes() / 1024), (unsigned)(LittleFS.totalBytes() / 1024));
    if (!loadConfig(cfg)) { Serial.println("Privzete nastavitve"); saveConfig(cfg); }
    if (!loadStations()) { defaultStations(); saveStations(); }

    std::vector<String> err, warn;
    if (!validateConfig(cfg, err, warn)) {
        for (auto& e : err) diagEvent("Napaka v nastavitvah: %s", e.c_str());
        safeMode = true;
        diagSetSafe(true, err.empty() ? "napaka v nastavitvah" : err[0].c_str(), fails);
    } else if (safeMode) {
        diagSetSafe(true, "3 zaporedni neuspeli zagoni (zagonska zanka)", fails);
    } else diagSetSafe(false, "", fails);

    if (!safeMode) {
        displayBegin();
        inputBegin(cfg);
        playerBegin(cfg.i2s_bck, cfg.i2s_lck, cfg.i2s_din);
    }
    appBegin();

    WiFi.persistent(false);
    WiFi.setHostname(cfg.hostname.c_str());
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);              // brez varčevanja – stabilnejši pretok zvoka
    WiFi.setAutoReconnect(true);

    bool connected = false;
    if (cfg.ssid.length()) {
        appSetInfo(SCR_BOOT, "Povezujem WiFi", cfg.ssid);
        WiFi.begin(cfg.ssid.c_str(), cfg.pass.c_str());
        uint32_t t0 = millis();
        while (millis() - t0 < 20000 && WiFi.status() != WL_CONNECTED) { appLoop(); delay(20); }
        connected = WiFi.status() == WL_CONNECTED;
    }

    if (!connected) {
        WiFi.disconnect(true);
        WiFi.mode(WIFI_AP);
        String n = apName();
        WiFi.softAP(n.c_str(), AP_PASS);
        Serial.printf("AP: %s / %s  http://%s\n", n.c_str(), AP_PASS, WiFi.softAPIP().toString().c_str());
        appSetInfo(SCR_AP, "WiFi: " + n, String("Geslo: ") + AP_PASS, "http://" + WiFi.softAPIP().toString());
    } else {
        Serial.printf("IP: %s\n", WiFi.localIP().toString().c_str());
        configTzTime(cfg.tz.c_str(), cfg.ntp.c_str());
        if (MDNS.begin(cfg.hostname.c_str())) MDNS.addService("http", "tcp", 80);
        if (safeMode) appSetInfo(SCR_SAFE, "Popravi nastavitve", WiFi.localIP().toString(), "http://" + cfg.hostname + ".local");
        else {
            appSetInfo(SCR_BOOT, "Povezano", WiFi.localIP().toString(), cfg.hostname + ".local");
            delay(1200);
            appStart();
        }
    }
    webBegin(!connected);
    if (!safeMode) logoBegin();
    bootStableAt = millis() + 20000;
}

void loop() {
    webLoop();
    appLoop();
    diagLoop();
    if (bootStableAt && millis() > bootStableAt) {   // zagon je uspel -> ponastavi števec
        bootStableAt = 0;
        { FlashGuard g("zagon"); sys.putUChar("bootfail", 0); }
    }
    vTaskDelay(pdMS_TO_TICKS(2));
}
