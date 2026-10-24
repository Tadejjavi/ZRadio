#include "display.h"
#include "app.h"
#include "config.h"
#include "player.h"
#include "stations.h"
#include <U8g2lib.h>
#include <WiFi.h>
#include <time.h>
#include <Wire.h>
#include "diag.h"
#include "display_tft.h"

static U8G2* u8 = nullptr;
static uint32_t lastDraw = 0;
static String   toast;
static uint32_t toastUntil = 0;
static uint8_t  curContrast = 0;
static bool     powerSave = false;
static uint32_t reinitAt = 0, lastProbe = 0, lastRefresh = 0;
static bool     lost = false;
static uint8_t  sent[1024];          // zadnja poslana slika (za pošiljanje le spremenjenih vrstic in predogled v panelu)
static bool     sentValid = false;
static uint32_t sentAt = 0;          // čas zadnjega dejanskega pošiljanja na zaslon
static portMUX_TYPE sentMux = portMUX_INITIALIZER_UNLOCKED;
static uint8_t  vuPkL = 0, vuPkR = 0;

// Pisave: _te = razširjena latinica (č, š, ž, ć, đ)
#define F_HEAD   u8g2_font_helvB08_te
#define F_TEXT   u8g2_font_helvR10_te
#define F_SMALL  u8g2_font_helvR08_te
#define F_FOOT   u8g2_font_5x7_tr
#define F_BIGNUM u8g2_font_logisoso24_tn
#define F_CLOCK  u8g2_font_logisoso32_tn

// ---------------------------------------------------------------------------
// Drseče besedilo (marquee)
// ---------------------------------------------------------------------------
struct Marquee {
    String   text;
    int16_t  off = 0;
    uint32_t pauseUntil = 0;
    void set(const String& t) { if (t != text) { text = t; off = 0; pauseUntil = millis() + 1500; } }
    // x,y = začetek osnovnice, w = širina okna; vrne true, če se premika
    bool draw(int x, int y, int w) {
        int tw = u8->getUTF8Width(text.c_str());
        if (tw <= w) { u8->drawUTF8(x, y, text.c_str()); return false; }
        int asc = u8->getAscent(), desc = u8->getDescent();
        u8->setClipWindow(x, y - asc - 1, x + w, y - desc + 1);
        const int gap = 28;
        u8->drawUTF8(x - off, y, text.c_str());
        u8->drawUTF8(x - off + tw + gap, y, text.c_str());
        u8->setMaxClipWindow();
        if (millis() > pauseUntil) {
            off += 1;
            if (off >= tw + gap) { off = 0; pauseUntil = millis() + 2000; }
        }
        return true;
    }
};
static Marquee mqHead, mqLine1, mqLine2, mqSaver;

// ---------------------------------------------------------------------------
static void drawWifi(int x, int y, uint8_t color) {   // 4 stolpci, levo spodaj = (x,y)
    int bars = 0;
    if (WiFi.status() == WL_CONNECTED) {
        int r = WiFi.RSSI();
        bars = r > -55 ? 4 : r > -65 ? 3 : r > -75 ? 2 : r > -85 ? 1 : 0;
    }
    u8->setDrawColor(color);
    for (int i = 0; i < 4; i++) {
        int h = 2 + i * 2;
        if (i < bars) u8->drawBox(x + i * 3, y - h, 2, h);
        else          u8->drawPixel(x + i * 3, y - 1);
    }
    u8->setDrawColor(1);
}

static void drawSpeaker(int x, int y, bool muted) {   // 7x7, levo zgoraj
    u8->drawBox(x, y + 2, 2, 3);
    u8->drawLine(x + 2, y + 2, x + 4, y);
    u8->drawLine(x + 2, y + 4, x + 4, y + 6);
    u8->drawVLine(x + 4, y, 7);
    if (muted) { u8->drawLine(x + 6, y + 1, x + 9, y + 5); u8->drawLine(x + 9, y + 1, x + 6, y + 5); }
}

static void drawBar(int x, int y, int w, int h, int val, int max) {
    u8->drawFrame(x, y, w, h);
    int fw = max > 0 ? (long)(w - 2) * val / max : 0;
    if (fw > 0) u8->drawBox(x + 1, y + 1, fw, h - 2);
}

static String clockStr(bool secs = false) {
    if (!appTimeValid()) return "--:--";
    time_t now = time(nullptr); struct tm tm; localtime_r(&now, &tm);
    char b[10];
    if (secs) strftime(b, sizeof(b), "%H:%M:%S", &tm); else strftime(b, sizeof(b), "%H:%M", &tm);
    return b;
}

// ura z utripajočim dvopičjem (vidno, da zaslon ni zamrznil)
static void drawClock(int x, int y, const String& c) {
    u8->drawStr(x, y, c.c_str());
    if (!appTimeValid() || (millis() / 500) % 2 == 0) return;
    int ci = c.indexOf(':'); if (ci < 0) return;
    int cx = x + u8->getStrWidth(c.substring(0, ci).c_str());
    int cw = u8->getStrWidth(":");
    u8->setDrawColor(0);
    u8->drawBox(cx, y - u8->getAscent(), cw, u8->getAscent() + 1);
    u8->setDrawColor(1);
}

static String dateStr() {
    if (!appTimeValid()) return "";
    static const char* dn[] = {"ned", "pon", "tor", "sre", "čet", "pet", "sob"};
    time_t now = time(nullptr); struct tm tm; localtime_r(&now, &tm);
    char b[24]; snprintf(b, sizeof(b), "%s, %d. %d.", dn[tm.tm_wday], tm.tm_mday, tm.tm_mon + 1);
    return b;
}

static void centerUTF8(int y, const char* s) {
    int w = u8->getUTF8Width(s);
    u8->drawUTF8((128 - w) / 2, y, s);
}

// ---------------------------------------------------------------------------
// Zasloni
// ---------------------------------------------------------------------------
static bool drawMain(const PlayerStatus& ps) {
    bool anim = false;
    const Station* s = (ui.cur >= 0 && ui.cur < (int)stations.size()) ? &stations[ui.cur] : nullptr;

    // glava: invertirana vrstica s številko in imenom postaje
    u8->drawBox(0, 0, 128, 12);
    u8->setDrawColor(0);
    u8->setFont(F_HEAD);
    char num[6]; snprintf(num, sizeof(num), "%02d", ui.cur + 1);
    u8->drawStr(2, 9, num);
    mqHead.set(s ? s->name : String("Ni postaj"));
    anim |= mqHead.draw(17, 9, 94);
    u8->setDrawColor(1);
    drawWifi(115, 10, 0);

    // naslov: "Izvajalec - Pesem" -> dve vrstici
    String l1, l2;
    String t = ps.title;
    int dash = t.indexOf(" - ");
    if (dash > 0) { l1 = t.substring(0, dash); l2 = t.substring(dash + 3); }
    else if (t.length()) { l1 = t; l2 = s ? s->genre : ""; }
    else { l1 = ps.icyName.length() ? ps.icyName : (s ? s->name : ""); l2 = s ? s->genre : ""; }
    u8->setFont(F_TEXT);
    mqLine1.set(l1); anim |= mqLine1.draw(0, 25, 128);
    u8->setFont(F_SMALL);
    mqLine2.set(l2); anim |= mqLine2.draw(0, 38, 128);

    // glasnost
    drawSpeaker(0, 42, ui.mute);
    drawBar(13, 43, 98, 5, ui.mute ? 0 : ui.vol, VOL_STEPS);
    u8->setFont(F_FOOT);
    char v[4]; snprintf(v, sizeof(v), "%d", ui.vol);
    u8->drawStr(128 - u8->getStrWidth(v), 49, v);

    // noga
    u8->drawHLine(0, 53, 128);
    String foot;
    if (toast.length() && millis() < toastUntil) foot = toast;
    else if (ps.connecting) foot = "Povezujem...";
    else if (!ps.wanted) foot = "Ustavljeno";
    else if (!ps.playing) foot = ps.reconnects ? "Ponovno povezujem..." : "Povezujem...";
    else {
        char b[24];
        String c = ps.codec; c.toUpperCase();
        if (ps.bitrate) snprintf(b, sizeof(b), "%s %luk", c.c_str(), (unsigned long)(ps.bitrate / 1000));
        else snprintf(b, sizeof(b), "%s", c.c_str());
        foot = b;
        if (ui.favOnly) foot += " *";
    }
    // šumniki v nogi niso podprti v F_FOOT -> uporabi F_SMALL za toast
    if (toast.length() && millis() < toastUntil) { u8->setFont(F_SMALL); u8->drawUTF8(0, 63, foot.c_str()); u8->setFont(F_FOOT); }
    else u8->drawStr(0, 62, foot.c_str());
    String clk = clockStr();
    drawClock(128 - u8->getStrWidth(clk.c_str()), 62, clk);

    // merilnik VU (L zgoraj, R spodaj) oz. opozorilo, ko medpomnilnik upade
    const int vx = 50, vw = 46;
    bool toastOn = toast.length() && millis() < toastUntil;
    if (ps.playing && !toastOn) {
        if (ps.bufferDs < 15) {
            if ((millis() / 400) % 2) { char b[12]; snprintf(b, sizeof(b), "buf %u.%us", ps.bufferDs / 10, ps.bufferDs % 10); u8->drawStr(vx, 62, b); }
        } else {
            auto len = [&](uint8_t v) { int l = (int)v * vw / 100; return l > vw ? vw : l; };
            int l = len(ps.vu >> 8), r = len(ps.vu & 0xFF);
            vuPkL = max<int>(l, vuPkL > 0 ? vuPkL - 1 : 0);
            vuPkR = max<int>(r, vuPkR > 0 ? vuPkR - 1 : 0);
            if (l) u8->drawBox(vx, 56, l, 2);
            if (r) u8->drawBox(vx, 60, r, 2);
            if (vuPkL) u8->drawVLine(vx + vuPkL, 56, 2);
            if (vuPkR) u8->drawVLine(vx + vuPkR, 60, 2);
        }
    } else { vuPkL = vuPkR = 0; }
    return anim;
}

static bool drawMenuO() {
    u8->drawBox(0, 0, 128, 11);
    u8->setDrawColor(0); u8->setFont(F_HEAD); u8->drawUTF8(2, 9, "Meni"); u8->setDrawColor(1);
    const int rows = 4, rh = 13;
    int n = appMenuCount();
    int first = constrain(ui.menuSel - 1, 0, max(0, n - rows));
    u8->setFont(F_SMALL);
    for (int r = 0; r < rows && first + r < n; r++) {
        int i = first + r, y = 13 + r * rh;
        bool sel = i == ui.menuSel;
        if (sel) { u8->drawRBox(0, y, 128, rh - 1, 2); u8->setDrawColor(0); }
        String lab, val; appMenuItem(i, lab, val);
        u8->drawUTF8(3, y + 10, lab.c_str());
        if (val.length()) u8->drawUTF8(125 - u8->getUTF8Width(val.c_str()), y + 10, val.c_str());
        u8->setDrawColor(1);
    }
    return false;
}

static void drawVolume() {
    u8->setFont(F_HEAD);
    centerUTF8(10, ui.mute ? "Utišano" : "Glasnost");
    u8->setFont(F_BIGNUM);
    char v[4]; snprintf(v, sizeof(v), "%d", ui.vol);
    u8->drawStr((128 - u8->getStrWidth(v)) / 2, 44, v);
    drawBar(4, 52, 120, 9, ui.mute ? 0 : ui.vol, VOL_STEPS);
}

static bool drawBrowse() {
    bool anim = false;
    // seznam (upošteva "samo priljubljene")
    bool fav = ui.favOnly;
    bool anyFav = false; for (auto& s : stations) if (s.fav) { anyFav = true; break; }
    std::vector<int> idx;
    for (int i = 0; i < (int)stations.size(); i++) if (!(fav && anyFav) || stations[i].fav) idx.push_back(i);
    int sel = 0;
    for (int i = 0; i < (int)idx.size(); i++) if (idx[i] == ui.browse) sel = i;

    u8->drawBox(0, 0, 128, 11);
    u8->setDrawColor(0);
    u8->setFont(F_HEAD);
    u8->drawUTF8(2, 9, fav && anyFav ? "Priljubljene" : "Postaje");
    char cnt[12]; snprintf(cnt, sizeof(cnt), "%d/%d", sel + 1, (int)idx.size());
    u8->drawStr(126 - u8->getStrWidth(cnt), 9, cnt);
    u8->setDrawColor(1);

    const int rows = 4, rh = 13;
    int first = constrain(sel - 1, 0, max(0, (int)idx.size() - rows));
    u8->setFont(F_SMALL);
    for (int r = 0; r < rows && first + r < (int)idx.size(); r++) {
        int i = idx[first + r];
        int y = 13 + r * rh;
        bool isSel = (first + r) == sel;
        if (isSel) { u8->drawRBox(0, y, 128, rh - 1, 2); u8->setDrawColor(0); }
        if (i == ui.cur) u8->drawTriangle(2, y + 3, 2, y + 9, 6, y + 6);
        char n[5]; snprintf(n, sizeof(n), "%02d", i + 1);
        u8->drawStr(9, y + 10, n);
        if (isSel) { mqSaver.set(stations[i].name); anim |= mqSaver.draw(24, y + 10, 102); }
        else {
            u8->setClipWindow(24, y, 126, y + rh);
            u8->drawUTF8(24, y + 10, stations[i].name.c_str());
            u8->setMaxClipWindow();
        }
        if (stations[i].fav) u8->drawStr(isSel ? 122 : 122, y + 10, "*");
        u8->setDrawColor(1);
    }
    return anim;
}

static void drawStandby() {
    if (!cfg.standby_clock) return;
    u8->setFont(F_CLOCK);
    String c = clockStr();
    drawClock((128 - u8->getStrWidth(c.c_str())) / 2, 40, c);
    u8->setFont(F_SMALL);
    centerUTF8(58, dateStr().c_str());
}

static void drawSaver(const PlayerStatus& ps) {
    // ura se vsakih 20 s premakne -> preprečuje vžiganje OLED-a
    uint32_t slot = millis() / 20000;
    int dx = (slot * 7) % 24 - 12, dy = (slot * 5) % 10 - 5;
    u8->setFont(F_BIGNUM);
    String c = clockStr();
    u8->drawStr((128 - u8->getStrWidth(c.c_str())) / 2 + dx, 34 + dy, c.c_str());
    u8->setFont(F_SMALL);
    const Station* s = (ui.cur >= 0 && ui.cur < (int)stations.size()) ? &stations[ui.cur] : nullptr;
    if (s) {
        int w = u8->getUTF8Width(s->name.c_str());
        u8->drawUTF8(max(0, (128 - w) / 2 + dx), 52 + dy, s->name.c_str());
    }
}

static void drawInfo() {
    u8->setFont(F_HEAD);
    u8->drawBox(0, 0, 128, 12);
    u8->setDrawColor(0);
    centerUTF8(9, ui.screen == SCR_SAFE ? "VARNI NAČIN" : ui.screen == SCR_AP ? "Nastavitev WiFi" : "ZRadio");
    u8->setDrawColor(1);
    u8->setFont(F_SMALL);
    centerUTF8(28, ui.info1.c_str());
    centerUTF8(42, ui.info2.c_str());
    centerUTF8(56, ui.info3.c_str());
}

// ---------------------------------------------------------------------------
bool displayBegin() {
    if (cfg.disp == "ili9341") return tftBegin();
    if (cfg.disp == "none" || cfg.sda < 0 || cfg.scl < 0) return false;
    const u8g2_cb_t* rot = cfg.rot180 ? U8G2_R2 : U8G2_R0;
    uint8_t rst = cfg.disp_rst >= 0 ? cfg.disp_rst : U8X8_PIN_NONE;
    if (cfg.disp == "sh1106")       u8 = new U8G2_SH1106_128X64_NONAME_F_HW_I2C(rot, rst, cfg.scl, cfg.sda);
    else if (cfg.disp == "ssd1309") u8 = new U8G2_SSD1309_128X64_NONAME2_F_HW_I2C(rot, rst, cfg.scl, cfg.sda);
    else                            u8 = new U8G2_SSD1306_128X64_NONAME_F_HW_I2C(rot, rst, cfg.scl, cfg.sda);
    u8->setI2CAddress(cfg.i2c_addr * 2);
    u8->setBusClock(400000);
    delay(500);                      // cenejši OLED-i rabijo čas, da se napajanje ustali
    if (!u8->begin()) { delete u8; u8 = nullptr; return false; }
    reinitAt = millis() + 2000;      // druga inicializacija – rešuje črn zaslon po hladnem zagonu
    u8->enableUTF8Print();
    u8->setFontMode(1);
    displayApplyConfig();
    return true;
}

// Ponovno pošlje zagonsko zaporedje krmilniku (ne obremenjuje zaslona – le zapis registrov)
static void reinit(const char* why) {
    diagCount(DC_OLED_REINIT);
    diagEvent("Zaslon: ponovna inicializacija (%s)", why);
    u8->initDisplay();
    u8->setPowerSave(powerSave);
    curContrast = 0;                 // kontrast se ponovno nastavi ob izrisu
    lastDraw = 0;
    sentValid = false;               // naslednjič pošlji celo sliko
}

static bool probe() {               // ali krmilnik potrdi svoj naslov na I2C
    Wire.beginTransmission(cfg.i2c_addr);
    return Wire.endTransmission() == 0;
}

void displayApplyConfig() {
    if (tftActive()) { tftApplyConfig(); return; }
    if (!u8) return;
    u8->setDisplayRotation(cfg.rot180 ? U8G2_R2 : U8G2_R0);
    curContrast = 0;   // ponovno nastavi ob naslednjem izrisu
}

void displayToast(const char* msg) {
    toast = msg; toastUntil = millis() + 2000;
}

void displayUpdate(bool force) {
    if (tftActive()) { tftUpdate(force, toast.length() && millis() < toastUntil ? toast.c_str() : nullptr); return; }
    if (!u8) return;
    uint32_t now = millis();

    // varovalke: druga inicializacija ob zagonu, zaznava izgube povezave, periodično osveževanje
    if (reinitAt && now > reinitAt) { reinitAt = 0; reinit("zagon"); force = true; }
    if (now - lastProbe > 5000) {
        lastProbe = now;
        bool ok = probe();
        if (!ok && !lost) { lost = true; diagCount(DC_OLED_LOST); diagEvent("Zaslon ne odgovarja na I2C (naslov 0x%02X)", cfg.i2c_addr); }
        else if (ok && lost) { lost = false; reinit("I2C znova dosegljiv"); force = true; }
    }
    if (lost) return;
    if (cfg.refresh_min && now - lastRefresh > (uint32_t)cfg.refresh_min * 60000UL) {
        lastRefresh = now; reinit("periodično"); force = true;
    }

    uint32_t interval = 60;                                 // ~16 sl/s za drseče besedilo
    if (ui.screen == SCR_STANDBY) interval = 250;          // utripajoče dvopičje
    if (ui.screen == SCR_SAVER) interval = 1000;
    if (!force && now - lastDraw < interval) return;
    lastDraw = now;

    // kontrast (nočni način, pripravljenost)
    uint8_t ct = (ui.night || ui.screen == SCR_STANDBY || ui.screen == SCR_SAVER) ? cfg.contrast_night : cfg.contrast;
    if (ct != curContrast) { u8->setContrast(ct); curContrast = ct; }

    bool off = ui.screen == SCR_STANDBY && !cfg.standby_clock;
    if (off != powerSave) { u8->setPowerSave(off); powerSave = off; sentValid = false; }
    if (off) return;

    PlayerStatus ps = playerStatus();
    u8->clearBuffer();
    switch (ui.screen) {
        case SCR_MAIN:    drawMain(ps); break;
        case SCR_VOLUME:  drawVolume(); break;
        case SCR_BROWSE:  drawBrowse(); break;
        case SCR_MENU:    drawMenuO(); break;
        case SCR_STANDBY: drawStandby(); break;
        case SCR_SAVER:   drawSaver(ps); break;
        default:          drawInfo(); break;
    }
    // pošlji le vrstice (8 pik), ki so se spremenile: VU ali ura = ~3 ms namesto ~25 ms za celo sliko
    uint8_t* buf = u8->getBufferPtr();
    bool full = !sentValid || force;
    for (uint8_t row = 0; row < 8; row++) {
        uint8_t* p = buf + row * 128;
        if (!full && memcmp(p, sent + row * 128, 128) == 0) continue;
        if (!full) u8->updateDisplayArea(0, row, 16, 1);
        portENTER_CRITICAL(&sentMux); memcpy(sent + row * 128, p, 128); sentAt = now; portEXIT_CRITICAL(&sentMux);
    }
    if (full) { u8->sendBuffer(); sentValid = true; }
}

size_t displaySnapshot(uint8_t* out, size_t max, uint32_t& ageMs, const char*& fmt) {
    if (tftActive()) { fmt = "rgb332-160x120"; return tftSnapshot(out, max, ageMs); }
    fmt = "mono-128x64";
    if (!u8 || !sentValid || max < 1024) return 0;
    portENTER_CRITICAL(&sentMux); memcpy(out, sent, 1024); uint32_t t = sentAt; portEXIT_CRITICAL(&sentMux);
    ageMs = millis() - t;
    return 1024;
}
