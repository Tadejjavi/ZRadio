#pragma once
#include <Arduino.h>
#include "config.h"

enum Screen : uint8_t { SCR_BOOT, SCR_AP, SCR_MAIN, SCR_VOLUME, SCR_BROWSE, SCR_STANDBY, SCR_SAVER, SCR_SAFE, SCR_MENU };

struct UiModel {
    Screen   screen = SCR_BOOT;
    int      cur = 0;            // trenutna postaja
    int      browse = 0;         // izbrana v brskanju
    uint8_t  vol = 10;
    bool     mute = false;
    bool     standby = false;
    bool     favOnly = false;
    bool     night = false;
    String   info1, info2, info3; // sporočila za BOOT/AP/SAFE
    uint32_t volOverlayUntil = 0; // barvni zaslon: glasnost kot okno čez glavni zaslon
    int      menuSel = 0;         // izbrana vrstica menija
    uint32_t sleepAt = 0;         // časovnik izklopa (millis), 0 = izklopljen
};

// meni na radiu (za izris)
int  appMenuCount();
void appMenuItem(int i, String& label, String& value);

extern UiModel ui;
extern bool safeMode;

// Ukazi iz spletnega strežnika (drugo opravilo) – varno, gredo prek vrste
enum AppCmdType : uint8_t { AC_ACTION, AC_PLAY, AC_VOL, AC_RELOAD_STATIONS, AC_APPLY_CONFIG, AC_REBOOT, AC_INPUT };
// AC_INPUT: arg = enkoder*10 + dogodek (1 = vrtenje +, 2 = vrtenje -, 3 = klik, 4 = dolg pritisk)
void appPost(AppCmdType t, int arg = 0);

void appBegin();
void appLoop();
void appSetInfo(Screen s, const String& a, const String& b = "", const String& c = "");
void appStart();                  // po WiFi povezavi: začni predvajati (autoplay)
bool appTimeValid();

// zaklep za branje stanja iz drugih opravil (web)
void appLock();
void appUnlock();
