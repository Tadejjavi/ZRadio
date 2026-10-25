#include "flashguard.h"
#include "app.h"
#include "stations.h"
#include "player.h"
#include "input.h"
#include "display.h"
#include "display_tft.h"
#include "diag.h"
#include <Preferences.h>
#include <time.h>

UiModel ui;
bool safeMode = false;

static QueueHandle_t     cmdQ = nullptr;
static SemaphoreHandle_t mtx  = nullptr;
static Preferences       rt;
static uint32_t volUntil = 0, browseUntil = 0, lastActivity = 0, saveAt = 0, menuUntil = 0;
static bool     menuCfgDirty = false;
static uint32_t cfgSaveAt = 0;           // zakasnjeno shranjevanje nastavitev (EQ z daljinca)
static int      digitNum = 0, digitCnt = 0;
static uint32_t digitUntil = 0;           // vnos številke postaje z daljinca
static bool     started = false;

struct AppCmd { AppCmdType t; int arg; };

void appLock()   { if (mtx) xSemaphoreTakeRecursive(mtx, portMAX_DELAY); }
void appUnlock() { if (mtx) xSemaphoreGiveRecursive(mtx); }

void appPost(AppCmdType t, int arg) {
    if (!cmdQ) return;
    AppCmd c{t, arg};
    xQueueSend(cmdQ, &c, pdMS_TO_TICKS(100));
}

bool appTimeValid() { return time(nullptr) > 1700000000; }

void appSetInfo(Screen s, const String& a, const String& b, const String& c) {
    appLock();
    ui.screen = s; ui.info1 = a; ui.info2 = b; ui.info3 = c;
    displayUpdate(true);       // izris vedno pod zaklepom (tftUpdate ga sprosti le za pošiljanje)
    appUnlock();
}

// ---------------------------------------------------------------------------
static void markDirty() { saveAt = millis() + 30000; }   // redkeje pisanje v flash
static void activity()  { lastActivity = millis(); if (ui.screen == SCR_SAVER) ui.screen = SCR_MAIN; }

static void ampSet(bool on) {
    if (cfg.amp_en < 0) return;
    digitalWrite(cfg.amp_en, (on ^ cfg.amp_inv) ? HIGH : LOW);
}

static bool hasFav() { for (auto& s : stations) if (s.fav) return true; return false; }
static bool inList(int i) { return !(ui.favOnly && hasFav()) || stations[i].fav; }

static int stepStation(int from, int dir) {
    int n = stations.size();
    if (n == 0) return -1;
    int i = from;
    for (int k = 0; k < n; k++) {
        i = (i + dir + n) % n;
        if (inList(i)) return i;
    }
    return from;
}

static void playIndex(int i) {
    if (i < 0 || i >= (int)stations.size()) return;
    ui.cur = i;
    ui.standby = false;
    ampSet(true);
    playerVolume(ui.mute ? 0 : ui.vol);
    playerPlay(stations[i].url);
    if (ui.screen != SCR_AP) ui.screen = SCR_MAIN;
    markDirty();
}

static void setStandby(bool on) {
    ui.standby = on;
    ui.sleepAt = 0;
    diagEvent(on ? "Pripravljenost" : "Vklop iz pripravljenosti");
    if (on) {
        playerStop();
        ampSet(false);
        ui.screen = SCR_STANDBY;
    } else {
        ui.screen = SCR_MAIN;
        playIndex(ui.cur);
    }
    markDirty();
}

static void showVolume() {
    if (cfg.disp == "ili9341" && (ui.screen == SCR_MAIN || ui.screen == SCR_SAVER)) {   // okno čez glavni zaslon
        ui.screen = SCR_MAIN; ui.volOverlayUntil = millis() + 1500; return;
    }
    if (ui.screen == SCR_MAIN || ui.screen == SCR_VOLUME || ui.screen == SCR_SAVER) {
        ui.screen = SCR_VOLUME; volUntil = millis() + 2000;
    }
}

static void changeVol(int d) {
    int v = constrain((int)ui.vol + d, 0, (int)cfg.vol_limit);
    ui.vol = v; ui.mute = false;
    playerMute(false);
    playerVolume(v);
    showVolume();
    markDirty();
}

static void enterBrowse(int dir) {
    if (stations.empty()) return;
    if (ui.screen != SCR_BROWSE) { ui.browse = ui.cur; ui.screen = SCR_BROWSE; }
    if (dir) ui.browse = stepStation(ui.browse, dir > 0 ? 1 : -1);
    browseUntil = millis() + 8000;
}

// ---------------------------------------------------------------------------
// Meni na radiu
// ---------------------------------------------------------------------------
struct EqPreset { const char* name; int8_t l, m, h; };
static const EqPreset EQP[] = {
    {"Ravno", 0, 0, 0}, {"Bas", 6, 0, 0}, {"Govor", -4, 3, 1}, {"Pop", 3, 0, 3}, {"Rock", 4, -2, 4}, {"Nočno", -3, 1, -2},
};
static const int EQN = sizeof(EQP) / sizeof(EQP[0]);
static const char* VIZ_ID[]  = {"vu", "spectrum", "mirror", "analog", "scope"};
static const char* VIZ_LBL[] = {"VU", "Spekter", "Zrcalni", "Igle", "Osciloskop"};
static const uint8_t SLEEP_MIN[] = {0, 15, 30, 60, 90};
enum MenuItem { MI_SLEEP, MI_EQ, MI_VIZ, MI_LAYOUT, MI_THEME, MI_NTHEME, MI_BRIGHT, MI_FAV, MI_STANDBY, MI_CLOSE, MI_COUNT };
static const char* LAYOUT_LBL[] = {"Klasična", "Kartica", "Radijska skala", "Velika ura", "Celozaslonski", "Obroči", "Gramofon", "Kaseta", "Sintwave 80'", "Nixie ura", "Krožni spekter", "Ambient"};

int appMenuCount() { return MI_COUNT; }

static int eqIndex() {
    for (int i = 0; i < EQN; i++) if (EQP[i].l == cfg.eq_low && EQP[i].m == cfg.eq_mid && EQP[i].h == cfg.eq_high) return i;
    return -1;
}
static int vizIndex() { for (int i = 0; i < 5; i++) if (cfg.viz == VIZ_ID[i]) return i; return 0; }

void appMenuItem(int i, String& label, String& value) {
    value = "";
    switch (i) {
        case MI_SLEEP:
            label = "Časovnik izklopa";
            if (ui.sleepAt) { uint32_t left = (ui.sleepAt - millis() + 59999) / 60000; value = String(left) + " min"; }
            else value = "izklop";
            break;
        case MI_EQ:     label = "Izenačevalnik"; { int e = eqIndex(); value = e >= 0 ? EQP[e].name : "po meri"; } break;
        case MI_VIZ:    label = "Prikaz zvoka"; value = VIZ_LBL[vizIndex()]; break;
        case MI_LAYOUT: label = "Postavitev"; value = LAYOUT_LBL[cfg.layout % 12]; break;
        case MI_THEME:  label = "Tema"; value = tftThemeName(cfg.theme); break;
        case MI_NTHEME: label = "Nočna tema"; value = cfg.night_theme < 0 ? "brez" : tftThemeName(cfg.night_theme); break;
        case MI_BRIGHT: label = "Svetlost"; value = String((cfg.contrast * 100 + 127) / 255) + " %"; break;
        case MI_FAV:    label = "Samo priljubljene"; value = ui.favOnly ? "da" : "ne"; break;
        case MI_STANDBY:label = "Pripravljenost"; break;
        default:        label = "Zapri"; break;
    }
}

static void sleepCycle() {
    int cur = 0;
    if (ui.sleepAt) { uint32_t left = (ui.sleepAt - millis()) / 60000; for (int k = 1; k < 5; k++) if (left < SLEEP_MIN[k]) { cur = k; break; } }
    int nx = (cur + 1) % 5;
    ui.sleepAt = SLEEP_MIN[nx] ? millis() + SLEEP_MIN[nx] * 60000UL : 0;
    diagEvent(ui.sleepAt ? "Časovnik izklopa: %u min" : "Časovnik izklopa izklopljen", SLEEP_MIN[nx]);
}
static int eqCycle() {
    int e = (eqIndex() + 1) % EQN;
    cfg.eq_low = EQP[e].l; cfg.eq_mid = EQP[e].m; cfg.eq_high = EQP[e].h;
    playerTone(cfg.eq_low, cfg.eq_mid, cfg.eq_high);
    return e;
}
static void digitShow() {
    char b[32]; snprintf(b, sizeof(b), "Postaja %d_", digitNum); displayToast(b);
}
static void digitCommit() {
    int n = digitNum; digitNum = digitCnt = 0; digitUntil = 0;
    if (n >= 1 && n <= (int)stations.size()) playIndex(n - 1);
    else { char b[32]; snprintf(b, sizeof(b), "Ni postaje %d", n); displayToast(b); }
}

static void menuOpen()  { ui.menuSel = 0; ui.screen = SCR_MENU; menuUntil = millis() + 15000; }
static void menuClose() {
    ui.screen = SCR_MAIN;
    if (menuCfgDirty) { menuCfgDirty = false; saveConfig(cfg); diagEvent("Nastavitve shranjene (meni)"); }
}
static void menuActivate() {
    menuUntil = millis() + 15000;
    switch (ui.menuSel) {
        case MI_SLEEP: sleepCycle(); break;
        case MI_EQ: eqCycle(); menuCfgDirty = true; break;
        case MI_VIZ:    cfg.viz = VIZ_ID[(vizIndex() + 1) % 5]; menuCfgDirty = true; break;
        case MI_LAYOUT: cfg.layout = (cfg.layout + 1) % 12; menuCfgDirty = true; break;
        case MI_THEME:  cfg.theme = (cfg.theme + 1) % tftThemeCount(); menuCfgDirty = true; break;
        case MI_NTHEME: cfg.night_theme = cfg.night_theme + 1 >= tftThemeCount() ? -1 : cfg.night_theme + 1; menuCfgDirty = true; break;
        case MI_BRIGHT: {
            int p = (cfg.contrast * 100 + 127) / 255;
            p = p >= 100 ? 20 : ((p / 20) + 1) * 20;
            cfg.contrast = p * 255 / 100; displayApplyConfig(); menuCfgDirty = true; break;
        }
        case MI_FAV:     ui.favOnly = !ui.favOnly; markDirty(); break;
        case MI_STANDBY: menuClose(); setStandby(true); break;
        default:         menuClose(); break;
    }
}

static void doAction(Action a, bool rpt) {
    activity();
    bool digit = a >= ACT_D0 && a <= ACT_D9;
    if (ui.standby && (digit || a == ACT_CH_PREV || a == ACT_CH_NEXT)) setStandby(false);
    else if (ui.standby && a != ACT_POWER && a != ACT_PLAY_STOP && a != ACT_OK && a != ACT_MENU) return;
    if (digit) {
        if (rpt) return;
        digitNum = digitNum * 10 + (a - ACT_D0); digitCnt++;
        digitUntil = millis() + 1500;
        if (digitCnt >= 3 || digitNum * 10 > (int)stations.size()) digitCommit();   // več številk ne more biti
        else digitShow();
        return;
    }
    switch (a) {
        case ACT_VOL_UP:   changeVol(+1); break;
        case ACT_VOL_DOWN: changeVol(-1); break;
        case ACT_NEXT:     if (!rpt) playIndex(stepStation(ui.cur, +1)); break;
        case ACT_PREV:     if (!rpt) playIndex(stepStation(ui.cur, -1)); break;
        case ACT_MUTE:
            ui.mute = !ui.mute; playerMute(ui.mute); showVolume(); break;
        case ACT_POWER:    setStandby(!ui.standby); break;
        case ACT_PLAY_STOP:
            if (ui.standby) setStandby(false);
            else if (ui.screen == SCR_BROWSE) playIndex(ui.browse);
            else if (playerStatus().wanted) { playerStop(); ampSet(false); }
            else playIndex(ui.cur);
            break;
        case ACT_FAV_TOGGLE:
            ui.favOnly = !ui.favOnly;
            displayToast(ui.favOnly ? "Samo priljubljene" : "Vse postaje");
            markDirty(); break;
        case ACT_BROWSE:   enterBrowse(0); break;
        case ACT_MENU:
            if (ui.standby) setStandby(false);
            else if (rpt) break;
            else if (ui.screen == SCR_BROWSE) playIndex(ui.browse);     // v seznamu postaj: potrdi izbiro
            else menuOpen();
            break;
        case ACT_CH_PREV:
        case ACT_CH_NEXT: {
            int d = a == ACT_CH_NEXT ? 1 : -1;
            if (ui.screen == SCR_BROWSE) enterBrowse(d);
            else if (!rpt) playIndex(stepStation(ui.cur, d));
            break;
        }
        case ACT_EQ: {
            if (rpt) break;
            int e = eqCycle();
            char b[40]; snprintf(b, sizeof(b), "Izenačevalnik: %s", EQP[e].name); displayToast(b);
            cfgSaveAt = millis() + 5000; break;
        }
        case ACT_SLEEP: {
            if (rpt) break;
            sleepCycle();
            char b[40];
            if (ui.sleepAt) snprintf(b, sizeof(b), "Izklop čez %lu min", (unsigned long)((ui.sleepAt - millis() + 59999) / 60000));
            else snprintf(b, sizeof(b), "Časovnik izklopa izklopljen");
            displayToast(b); break;
        }
        case ACT_OK:
            if (ui.standby) setStandby(false);
            else if (ui.screen == SCR_BROWSE) playIndex(ui.browse);
            else enterBrowse(0);
            break;
        default: break;
    }
}

static void handleInput(const InputEvent& e) {
    bool single = !cfg.enc2.en;   // samo en enkoder: klik = brskanje postaj
    // prvi dotik med ohranjevalnikom zaslona samo prebudi zaslon (brez spremembe glasnosti/postaje)
    if (ui.screen == SCR_SAVER) { activity(); return; }
    if (ui.screen == SCR_MENU) {
        activity();
        if (e.type == IN_ENC_TURN) {
            ui.menuSel = constrain(ui.menuSel + (e.val > 0 ? 1 : -1), 0, MI_COUNT - 1);
            menuUntil = millis() + 15000;
        } else if (e.type == IN_ENC_CLICK) menuActivate();
        else if (e.type == IN_ENC_LONG) menuClose();
        else if ((e.type == IN_BTN || e.type == IN_IR) && (e.val == ACT_CH_PREV || e.val == ACT_CH_NEXT)) {
            ui.menuSel = constrain(ui.menuSel + (e.val == ACT_CH_NEXT ? 1 : -1), 0, MI_COUNT - 1);
            menuUntil = millis() + 15000;
        }
        else if ((e.type == IN_BTN || e.type == IN_IR) && (e.val == ACT_MENU || e.val == ACT_OK)) { if (!e.rpt) menuActivate(); }
        else if (e.type == IN_BTN || e.type == IN_IR) { menuClose(); doAction((Action)e.val, e.rpt); }
        return;
    }
    switch (e.type) {
        case IN_ENC_TURN:
            activity();
            if (ui.standby) return;
            if (e.src == 2 || (single && ui.screen == SCR_BROWSE)) {
                enterBrowse(e.val);
            } else {
                changeVol(e.val);
            }
            break;
        case IN_ENC_CLICK:
            if (e.src == 1 && !single) doAction(ACT_MUTE, false);
            else doAction(ACT_OK, false);
            break;
        case IN_ENC_LONG:
            activity();
            if (ui.standby) { doAction(ACT_POWER, false); break; }
            if (e.src == 1 && !single) doAction(ACT_POWER, false);   // enkoder 1: vklop/izklop
            else menuOpen();                                          // enkoder 2 (ali edini): meni
            break;
        case IN_BTN:
        case IN_IR:
            doAction((Action)e.val, e.rpt);
            break;
        default: break;
    }
}

static void handleCmd(const AppCmd& c) {
    switch (c.t) {
        case AC_ACTION: doAction((Action)c.arg, false); break;
        case AC_PLAY:   activity(); playIndex(c.arg); break;
        case AC_VOL:    activity(); changeVol(c.arg - ui.vol); break;
        case AC_RELOAD_STATIONS:
            if (ui.cur >= (int)stations.size()) ui.cur = 0;
            if (ui.browse >= (int)stations.size()) ui.browse = 0;
            break;
        case AC_APPLY_CONFIG:
            playerTone(cfg.eq_low, cfg.eq_mid, cfg.eq_high);
            playerMono(cfg.mono);
            if (ui.vol > cfg.vol_limit) { ui.vol = cfg.vol_limit; playerVolume(ui.vol); }
            configTzTime(cfg.tz.c_str(), cfg.ntp.c_str());
            displayApplyConfig();
            break;
        case AC_INPUT: {                       // virtualni enkoder iz panela
            InputEvent e;
            e.src = constrain(c.arg / 10, 1, 2);
            switch (c.arg % 10) {
                case 1: e.type = IN_ENC_TURN; e.val = 1; break;
                case 2: e.type = IN_ENC_TURN; e.val = -1; break;
                case 3: e.type = IN_ENC_CLICK; break;
                case 4: e.type = IN_ENC_LONG; break;
                default: return;
            }
            handleInput(e);
            break;
        }
        case AC_REBOOT:
            if (saveAt) { FlashGuard g("stanje"); rt.putInt("st", ui.cur); rt.putUChar("vol", ui.vol); rt.putBool("fav", ui.favOnly); }
            diagEvent("Ponovni zagon (zahteva iz panela)");
            delay(300); ESP.restart(); break;
    }
}

static void updateNight() {
    static uint32_t t = 0;
    if (millis() - t < 10000 && t) return;
    t = millis();
    bool n = false;
    if (cfg.night_from != cfg.night_to && appTimeValid()) {
        time_t now = time(nullptr); struct tm tm; localtime_r(&now, &tm);
        int h = tm.tm_hour;
        n = cfg.night_from < cfg.night_to ? (h >= cfg.night_from && h < cfg.night_to)
                                          : (h >= cfg.night_from || h < cfg.night_to);
    }
    ui.night = n;
}

// ---------------------------------------------------------------------------
void appBegin() {
    mtx  = xSemaphoreCreateRecursiveMutex();
    cmdQ = xQueueCreate(16, sizeof(AppCmd));
    rt.begin("rt", false);
    ui.cur     = rt.getInt("st", 0);
    ui.vol     = min<uint8_t>(rt.getUChar("vol", 10), cfg.vol_limit);
    ui.favOnly = rt.getBool("fav", false);
    if (ui.cur < 0 || ui.cur >= (int)stations.size()) ui.cur = 0;
    if (cfg.amp_en >= 0 && !safeMode) { pinMode(cfg.amp_en, OUTPUT); ampSet(false); }
    lastActivity = millis();
}

void appStart() {
    if (started) return;
    started = true;
    playerTone(cfg.eq_low, cfg.eq_mid, cfg.eq_high);
    playerMono(cfg.mono);
    playerVolume(ui.vol);
    appLock();
    if (cfg.autoplay && !stations.empty()) playIndex(ui.cur);
    else { ui.screen = SCR_MAIN; }
    appUnlock();
}

void appLoop() {
    InputEvent e;
    AppCmd c;
    appLock();
    while (inputPoll(e)) handleInput(e);
    while (xQueueReceive(cmdQ, &c, 0) == pdTRUE) handleCmd(c);

    uint32_t now = millis();
    if (ui.screen == SCR_VOLUME && now > volUntil) ui.screen = SCR_MAIN;
    if (ui.screen == SCR_BROWSE && now > browseUntil) ui.screen = SCR_MAIN;
    if (ui.screen == SCR_MENU && now > menuUntil) menuClose();
    if (ui.sleepAt && !ui.standby && (int32_t)(now - ui.sleepAt) >= 0) { diagEvent("Časovnik izklopa potekel"); setStandby(true); }
    if (ui.screen == SCR_MAIN && cfg.saver_min && !ui.standby &&
        now - lastActivity > (uint32_t)cfg.saver_min * 60000UL) ui.screen = SCR_SAVER;
    if (digitUntil && now > digitUntil) digitCommit();
    if (cfgSaveAt && now > cfgSaveAt) { cfgSaveAt = 0; saveConfig(cfg); diagEvent("Nastavitve shranjene (daljinec)"); }
    updateNight();
    displayUpdate(false);      // pod zaklepom: splet lahko med tem menja seznam postaj
    appUnlock();

    if (saveAt && now > saveAt) {
        saveAt = 0;
        FlashGuard g("stanje");
        rt.putInt("st", ui.cur);
        rt.putUChar("vol", ui.vol);
        rt.putBool("fav", ui.favOnly);
    }
}
