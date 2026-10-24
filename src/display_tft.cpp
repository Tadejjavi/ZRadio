#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <U8g2lib.h>
#include <WiFi.h>
#include <time.h>
#include "display_tft.h"
#include "app.h"
#include "config.h"
#include "player.h"
#include "stations.h"
#include "diag.h"
#include "spectrum.h"
#include "logo.h"
#include "flashguard.h"
#include <LittleFS.h>

// ---------------------------------------------------------------------------
// Gonilnik: ILI9341 na SPI2 (FSPI). Pini iz nastavitev; MISO ni potreben.
// ---------------------------------------------------------------------------
class LGFX_ILI9341 : public lgfx::LGFX_Device {
public:
    lgfx::Panel_ILI9341 panel;
    lgfx::Bus_SPI       bus;
    lgfx::Light_PWM     light;
    LGFX_ILI9341() {
        {
            auto c = bus.config();
            c.spi_host    = SPI2_HOST;
            c.spi_mode    = 0;
            c.freq_write  = cfg.spi_mhz >= 80 ? 80000000 : 40000000;   // 80 MHz le s kratkimi žicami
            c.freq_read   = 16000000;
            c.spi_3wire   = false;
            c.use_lock    = true;
            c.dma_channel = SPI_DMA_CH_AUTO;
            c.pin_sclk = cfg.spi_sck; c.pin_mosi = cfg.spi_mosi; c.pin_miso = -1; c.pin_dc = cfg.spi_dc;
            bus.config(c);
            panel.setBus(&bus);
        }
        {
            auto p = panel.config();
            p.pin_cs = cfg.spi_cs; p.pin_rst = cfg.disp_rst; p.pin_busy = -1;
            p.panel_width = 240; p.panel_height = 320;
            p.offset_x = 0; p.offset_y = 0; p.offset_rotation = 0;
            p.readable = false; p.invert = false; p.rgb_order = false;
            p.dlen_16bit = false; p.bus_shared = false;
            panel.config(p);
        }
        if (cfg.bl >= 0) {
            auto l = light.config();
            l.pin_bl = cfg.bl; l.invert = false; l.freq = 12000; l.pwm_channel = 7;
            light.config(l);
            panel.setLight(&light);
        }
        setPanel(&panel);
    }
};

static LGFX_ILI9341* tft = nullptr;
static LGFX_Sprite*  spr = nullptr;
static constexpr int W = 320, H = 240, BAND = 8, NBANDS = H / BAND;
static uint32_t bandSum[NBANDS];
static bool     sumsValid = false;
static uint32_t lastDraw = 0, lastPush = 0, lastRefresh = 0;
static uint8_t  curBright = 255;
static bool     dark = false;

// pisave (u8g2, razširjena latinica za šumnike)
static const lgfx::U8g2font F_HEAD (u8g2_font_helvB14_te);
static const lgfx::U8g2font F_TITLE(u8g2_font_helvB18_te);
static const lgfx::U8g2font F_SUB  (u8g2_font_helvR14_te);
static const lgfx::U8g2font F_SMALL(u8g2_font_helvR10_te);
static const lgfx::U8g2font F_FOOT (u8g2_font_helvB10_te);
static const lgfx::U8g2font F_TINY (u8g2_font_helvR08_te);
static const lgfx::U8g2font F_VOL  (u8g2_font_logisoso42_tn);
static const lgfx::U8g2font F_CLOCK(u8g2_font_logisoso62_tn);

static const lgfx::U8g2font F_BIG  (u8g2_font_helvB24_te);

// ---------------------------------------------------------------------------
// Teme: komplet barv; vse risanje uporablja le te spremenljivke
// ---------------------------------------------------------------------------
struct RGB { uint8_t r, g, b; };
struct Theme { const char* name; RGB bg, hdr, acc, txt, sub, line, lo, mid, hi; };
static const Theme THEMES[] = {
    {"Modra noč",     {8,12,18},    {22,52,86},   {60,170,240}, {240,244,248}, {160,172,186}, {62,72,86},   {50,205,100}, {245,200,40}, {240,70,60}},
    {"Črna",          {0,0,0},      {24,24,24},   {0,200,255},  {255,255,255}, {170,170,170}, {60,60,60},   {0,220,120},  {255,210,0},  {255,50,50}},
    {"Svetla",        {232,236,240},{40,70,110},  {30,120,220}, {20,24,30},    {80,90,105},   {190,198,208},{30,160,80},  {220,160,0},  {210,50,40}},
    {"Jantar retro",  {12,8,2},     {55,32,5},    {255,170,30}, {255,200,110}, {200,140,60},  {70,45,12},   {200,120,20}, {255,170,30}, {255,90,20}},
    {"Zeleni fosfor", {0,10,4},     {0,42,16},    {60,255,120}, {140,255,170}, {70,190,110},  {0,60,25},    {30,150,70},  {60,220,110}, {170,255,190}},
    {"Nočna rdeča",   {6,0,0},      {45,0,0},     {200,30,30},  {225,70,55},   {160,45,40},   {55,8,8},     {120,15,15},  {170,25,20},  {230,50,40}},
    {"Sončni zahod",  {24,10,28},   {85,25,70},   {255,120,60}, {255,235,225}, {220,160,170}, {70,35,70},   {255,150,60}, {240,80,120}, {170,60,200}},
    {"Sintwave neon", {14,6,30},    {50,10,85},   {255,60,200}, {240,230,255}, {170,150,220}, {60,35,100},  {0,220,255},  {170,90,255}, {255,60,200}},
    {"Ocean",         {4,24,32},    {0,70,92},    {0,220,200},  {225,250,250}, {130,190,195}, {20,70,80},   {0,200,170},  {80,170,255}, {240,240,255}},
    {"Gozd",          {10,20,14},   {30,62,40},   {150,205,90}, {232,242,222}, {160,185,150}, {45,75,52},   {90,170,70},  {190,210,80}, {240,170,60}},
    {"Grafit",        {28,28,30},   {55,55,60},   {225,225,225},{245,245,245}, {160,160,165}, {75,75,80},   {150,150,155},{200,200,205},{255,255,255}},
    {"Stari radio",   {236,226,196},{110,70,35},  {175,60,30},  {50,35,20},    {110,85,55},   {200,185,150},{120,90,50},  {175,110,40}, {175,60,30}},
};
static constexpr int THEME_N = sizeof(THEMES) / sizeof(THEMES[0]);
const char* tftThemeName(int i) { return i >= 0 && i < THEME_N ? THEMES[i].name : ""; }
int tftThemeCount() { return THEME_N; }

static uint16_t C_BG, C_HDR, C_HTXT, C_ONACC, C_ACC, C_TXT, C_SUB, C_DIM, C_LINE, C_GRN, C_YEL, C_RED, C_GRN_D, C_YEL_D, C_RED_D;
static RGB      T_LO, T_HI, T_BG;                    // za prelive (zrcalni spekter)
static int      curTheme = -1;

static void applyTheme(int i) {
    if (i < 0 || i >= THEME_N) i = 0;
    const Theme& t = THEMES[i];
    auto c = [](RGB v) { return lgfx::color565(v.r, v.g, v.b); };
    auto mix = [](RGB a, RGB b, float k) { return lgfx::color565(a.r * k + b.r * (1 - k), a.g * k + b.g * (1 - k), a.b * k + b.b * (1 - k)); };
    auto lum = [](RGB v) { return 0.3f * v.r + 0.59f * v.g + 0.11f * v.b; };
    C_BG = c(t.bg); C_HDR = c(t.hdr); C_ACC = c(t.acc); C_TXT = c(t.txt); C_SUB = c(t.sub); C_LINE = c(t.line);
    C_DIM = mix(t.sub, t.bg, 0.7f);
    C_GRN = c(t.lo); C_YEL = c(t.mid); C_RED = c(t.hi);
    C_GRN_D = mix(t.lo, t.bg, 0.22f); C_YEL_D = mix(t.mid, t.bg, 0.22f); C_RED_D = mix(t.hi, t.bg, 0.22f);
    C_HTXT  = lum(t.hdr) > 140 ? lgfx::color565(20, 20, 20) : (lum(t.txt) > 140 ? c(t.txt) : lgfx::color565(255, 255, 255));
    C_ONACC = lum(t.acc) > 140 ? lgfx::color565(15, 15, 15) : lgfx::color565(255, 255, 255);
    T_LO = t.lo; T_HI = t.hi; T_BG = t.bg;
    curTheme = i;
}
static void initColors() { applyTheme(0); }

// ---------------------------------------------------------------------------
struct TMarq {
    String   t;
    int      off = 0;
    uint32_t pause = 0;
    void set(const String& s) { if (s != t) { t = s; off = 0; pause = millis() + 1500; } }
    // x, y = osnovnica levo; w = širina okna; top/bot = navpični obseg za obrez
    void draw(int x, int y, int w, int top, int bot) {
        int tw = spr->textWidth(t);
        if (tw <= w) { spr->drawString(t, x, y); return; }
        const int gap = 40;
        spr->setClipRect(x, top, w, bot - top);
        spr->drawString(t, x - off, y);
        spr->drawString(t, x - off + tw + gap, y);
        spr->clearClipRect();
        if (millis() > pause) { off += 2; if (off >= tw + gap) { off = 0; pause = millis() + 2500; } }
    }
};
static TMarq mHead, mL1, mL2, mSel;

// ---------------------------------------------------------------------------
static String clockStr() {
    if (!appTimeValid()) return "--:--";
    time_t now = time(nullptr); struct tm tm; localtime_r(&now, &tm);
    char b[8]; strftime(b, sizeof(b), "%H:%M", &tm); return b;
}
static String dateStr() {
    if (!appTimeValid()) return "";
    static const char* dn[] = {"nedelja", "ponedeljek", "torek", "sreda", "četrtek", "petek", "sobota"};
    time_t now = time(nullptr); struct tm tm; localtime_r(&now, &tm);
    char b[40]; snprintf(b, sizeof(b), "%s, %d. %d. %d", dn[tm.tm_wday], tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900);
    return b;
}
// ura z utripajočim dvopičjem; x = levi rob
static void drawClock(int x, int y, const String& c, uint16_t bg) {
    spr->drawString(c, x, y);
    if (!appTimeValid() || (millis() / 500) % 2 == 0) return;
    int ci = c.indexOf(':'); if (ci < 0) return;
    int cx = x + spr->textWidth(c.substring(0, ci)), cw = spr->textWidth(":");
    int h = spr->fontHeight();
    spr->fillRect(cx, y - h, cw, h + 2, bg);
}
static void center(const String& s, int y) { spr->drawString(s, (W - spr->textWidth(s)) / 2, y); }

static void drawWifi(int x, int y) {             // levo spodaj = (x, y)
    int bars = 0;
    if (WiFi.status() == WL_CONNECTED) {
        int r = WiFi.RSSI();
        bars = r > -55 ? 4 : r > -65 ? 3 : r > -75 ? 2 : r > -85 ? 1 : 0;
    }
    for (int i = 0; i < 4; i++) {
        int h = 6 + i * 4;
        spr->fillRect(x + i * 7, y - h, 5, h, i < bars ? C_TXT : C_DIM);
    }
}

static void drawSpeaker(int x, int y, bool muted, uint16_t col) {   // 16x14, levo zgoraj
    spr->fillRect(x, y + 4, 4, 6, col);
    spr->fillTriangle(x + 3, y + 7, x + 9, y, x + 9, y + 13, col);
    if (muted) { spr->drawLine(x + 11, y + 3, x + 16, y + 10, C_RED); spr->drawLine(x + 16, y + 3, x + 11, y + 10, C_RED); }
    else { spr->drawArc(x + 9, y + 7, 6, 5, -45, 45, col); }
}

// ---------------------------------------------------------------------------
// VU: segmentni merilnik z zadrževanjem vrha
// ---------------------------------------------------------------------------
static float   vuL = 0, vuR = 0;
static uint8_t pkL = 0, pkR = 0;
static uint32_t pkLt = 0, pkRt = 0;
static void vuBar(int x, int y, int w, int h, float lvl, uint8_t& pk, uint32_t& pkT) {
    const int segW = 9, gap = 2, n = (w + gap) / (segW + gap);
    int lit = (int)(lvl * n + 0.5f);
    if (lit >= pk) { pk = lit; pkT = millis(); }
    else if (millis() - pkT > 700 && pk > 0) { pk--; pkT = millis() - 620; }
    for (int i = 0; i < n; i++) {
        float f = (float)i / n;
        bool on = i < lit || (i == pk - 1 && pk > 0);
        uint16_t c = f < 0.65f ? (on ? C_GRN : C_GRN_D) : f < 0.85f ? (on ? C_YEL : C_YEL_D) : (on ? C_RED : C_RED_D);
        spr->fillRect(x + i * (segW + gap), y, segW, h, c);
    }
}

// ---------------------------------------------------------------------------
// Spekter: 24 logaritemskih pasov 40 Hz .. 16 kHz, segmentni stolpci z vrhovi
// ---------------------------------------------------------------------------
static constexpr int SPMAX = 48;
static float   spB[SPMAX];
static float   spPk[SPMAX];
static void drawSpectrum(int x0, int y0, int w, int h, const PlayerStatus& ps, int SPN = 24, int segH = 3) {
    if (SPN > SPMAX) SPN = SPMAX;
    spectrumCompute(spB, SPN, ps.sampleRate);
    const int step = w / SPN, bw = step - 2, segG = 1, nseg = h / (segH + segG);
    for (int b = 0; b < SPN; b++) {
        float v = ps.playing ? spB[b] : 0;
        if (v >= spPk[b]) spPk[b] = v; else spPk[b] = max(0.0f, spPk[b] - 0.012f);
        int lit = (int)(v * nseg + 0.5f), pk = (int)(spPk[b] * nseg + 0.5f);
        int x = x0 + b * step;
        for (int s = 0; s < nseg; s++) {
            int y = y0 + h - (s + 1) * (segH + segG);
            float f = (float)s / nseg;
            bool on = s < lit;
            uint16_t c = f < 0.6f ? (on ? C_GRN : C_GRN_D) : f < 0.85f ? (on ? C_YEL : C_YEL_D) : (on ? C_RED : C_RED_D);
            spr->fillRect(x, y, bw, segH, c);
        }
        if (pk > 0 && pk > lit) spr->fillRect(x, y0 + h - pk * (segH + segG), bw, segH, C_TXT);
    }
}

// ---------------------------------------------------------------------------
// Analogna merilnika VU z iglama (skala kot pri pravem VU: linearna v napetosti)
// ---------------------------------------------------------------------------
static constexpr int MW = 148, MH = 106;
static LGFX_Sprite* face = nullptr;
static float needle[2] = {0, 0}, needleV[2] = {0, 0};
static uint32_t peakLed[2] = {0, 0};
static const float NPX = MW / 2.0f, NPY = MH + 50, NR = 120, NANG = 36;   // os igle pod merilnikom
static float vuPos(float db) {                         // dB -> 0..1 po loku
    float v = powf(10.0f, db / 20.0f), v0 = powf(10.0f, -20 / 20.0f), v1 = powf(10.0f, 3 / 20.0f);
    return constrain((v - v0) / (v1 - v0), 0.0f, 1.0f);
}
static void buildFace() {
    face = new LGFX_Sprite(spr);
    face->setColorDepth(16); face->setPsram(true);
    if (!face->createSprite(MW, MH)) { delete face; face = nullptr; return; }
    uint16_t cream = lgfx::color565(236, 226, 196), ink = lgfx::color565(40, 30, 20), red = lgfx::color565(200, 40, 30);
    face->fillScreen(cream);
    auto pt = [&](float t, float r, float& x, float& y) { float a = (-NANG + 2 * NANG * t) * DEG_TO_RAD; x = NPX + r * sinf(a); y = NPY - r * cosf(a); };
    // lok in rdeče območje
    for (float t = 0; t <= 1.0f; t += 0.004f) { float x, y; pt(t, NR, x, y); face->drawPixel(x, y, ink); }
    for (float t = vuPos(0); t <= 1.0f; t += 0.003f) for (int k = 0; k < 4; k++) { float x, y; pt(t, NR + k, x, y); face->drawPixel(x, y, red); }
    const float marks[] = {-20, -10, -7, -5, -3, -2, -1, 0, 1, 2, 3};
    face->setFont(&F_TINY); face->setTextDatum(lgfx::textdatum_t::middle_center);
    for (float m : marks) {
        float t = vuPos(m), x0, y0, x1, y1; bool big = (m == -20 || m == -10 || m == -5 || m == 0 || m == 3 || m == -3);
        pt(t, NR, x0, y0); pt(t, NR - (big ? 10 : 6), x1, y1);
        face->drawWideLine(x0, y0, x1, y1, big ? 1.2f : 0.6f, m >= 0 ? red : ink);
        if (big) { float tx, ty; pt(t, NR - 20, tx, ty); char b[6]; snprintf(b, sizeof(b), m > 0 ? "+%d" : "%d", (int)m); face->setTextColor(m >= 0 ? red : ink); face->drawString(b, tx, ty); }
    }
    face->setFont(&F_HEAD); face->setTextColor(ink); face->drawString("VU", MW / 2, MH - 26);
}
static void drawMeter(int x0, int y0, int ch, float lvl, const char* lab) {
    // balistika: približek VU (~300 ms), z rahlim prenihajem
    float target = lvl;
    needleV[ch] = needleV[ch] * 0.55f + (target - needle[ch]) * 0.32f;
    needle[ch] = constrain(needle[ch] + needleV[ch], -0.02f, 1.04f);
    if (face) face->pushSprite(spr, x0, y0); else spr->fillRect(x0, y0, MW, MH, lgfx::color565(236, 226, 196));
    uint16_t ink = lgfx::color565(40, 30, 20), red = lgfx::color565(200, 40, 30), dimR = lgfx::color565(90, 30, 25);
    spr->setFont(&F_FOOT); spr->setTextColor(lgfx::color565(90, 80, 60)); spr->drawString(lab, x0 + 7, y0 + MH - 6);
    if (lvl >= vuPos(0)) peakLed[ch] = millis();
    spr->fillCircle(x0 + MW - 12, y0 + MH - 12, 4, millis() - peakLed[ch] < 400 ? lgfx::color565(255, 40, 30) : dimR);
    spr->setClipRect(x0 + 2, y0 + 2, MW - 4, MH - 4);
    float a = (-NANG + 2 * NANG * needle[ch]) * DEG_TO_RAD;
    spr->drawWideLine(x0 + NPX, y0 + NPY, x0 + NPX + (NR + 6) * sinf(a), y0 + NPY - (NR + 6) * cosf(a), 1.0f, ink);
    spr->fillEllipse(x0 + MW / 2, y0 + MH + 4, 16, 12, lgfx::color565(60, 50, 40));
    spr->clearClipRect();
    spr->drawRoundRect(x0, y0, MW, MH, 6, lgfx::color565(90, 80, 60));
    spr->drawRoundRect(x0 + 1, y0 + 1, MW - 2, MH - 2, 5, lgfx::color565(90, 80, 60));
    (void)red;
}
static void drawAnalog(const PlayerStatus& ps) {
    if (!face) buildFace();
    auto lvl = [](uint8_t v) {                         // 0..127 -> dB -> položaj na skali
        if (!v) return 0.0f;
        float db = -20.0f + 23.0f * powf(min(1.0f, v / 127.0f), 1.3f);
        return vuPos(db);
    };
    float l = ps.playing ? lvl(ps.vu >> 8) : 0, r = ps.playing ? lvl(ps.vu & 0xFF) : 0;
    drawMeter(8, 66, 0, l, "L");
    drawMeter(164, 66, 1, r, "R");
}

// ---------------------------------------------------------------------------
// Osciloskop
// ---------------------------------------------------------------------------
static int16_t scopeBuf[300];
static float   scopeGain = 1.0f;
static void drawScope(int x0, int y0, int w, int h, const PlayerStatus& ps) {
    uint16_t bg = lgfx::color565(0, 18, 8), grid = lgfx::color565(0, 50, 25), beam = lgfx::color565(60, 255, 120);
    spr->fillRect(x0, y0, w + 1, h + 1, bg);
    for (int gx = 0; gx <= w; gx += w / 8) spr->drawFastVLine(x0 + gx, y0, h + 1, grid);
    for (int gy = 0; gy <= h; gy += h / 4) spr->drawFastHLine(x0, y0 + gy, w + 1, grid);
    int n = min(w, 300);
    bool ok = ps.playing && spectrumScope(scopeBuf, n, 2);
    int mid = y0 + h / 2;
    if (!ok) { spr->drawFastHLine(x0, mid, w, beam); return; }
    int pk = 1; for (int i = 0; i < n; i++) pk = max(pk, abs((int)scopeBuf[i]));
    float want = (h * 0.45f) / pk;                       // samodejno ojačanje, počasi prilagojeno
    scopeGain = scopeGain * 0.85f + min(want, 0.02f) * 0.15f;
    int py = mid - (int)(scopeBuf[0] * scopeGain);
    for (int i = 1; i < n; i++) {
        int y = constrain(mid - (int)(scopeBuf[i] * scopeGain), y0, y0 + h);
        spr->drawLine(x0 + i - 1, py, x0 + i, y, beam);
        spr->drawLine(x0 + i - 1, py + 1, x0 + i, y + 1, lgfx::color565(30, 140, 70));
        py = y;
    }
}

// ---------------------------------------------------------------------------
// Zrcalni spekter z barvnim prelivom
// ---------------------------------------------------------------------------
static constexpr int MRN = 32;
static float mrB[MRN];
static void drawMirror(int x0, int y0, int w, int h, const PlayerStatus& ps) {
    spectrumCompute(mrB, MRN, ps.sampleRate);
    const int step = w / MRN, bw = step - 2, half = h / 2 - 1, mid = y0 + h / 2;
    for (int b = 0; b < MRN; b++) {
        int hh = ps.playing ? (int)(mrB[b] * half) : 0;
        if (hh < 1) hh = 1;
        int x = x0 + b * step;
        for (int yy = 0; yy < hh; yy++) {
            float t = (float)yy / half;                      // preliv: barva "nizko" -> "visoko" iz teme
            float r = T_LO.r + (T_HI.r - T_LO.r) * t, g = T_LO.g + (T_HI.g - T_LO.g) * t, bl = T_LO.b + (T_HI.b - T_LO.b) * t;
            spr->drawFastHLine(x, mid - 1 - yy, bw, lgfx::color565(r, g, bl));
            spr->drawFastHLine(x, mid + yy, bw, lgfx::color565(r * 0.4f + T_BG.r * 0.6f, g * 0.4f + T_BG.g * 0.6f, bl * 0.4f + T_BG.b * 0.6f));
        }
    }
}

// ---------------------------------------------------------------------------
// Zasloni
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// Skupni gradniki postavitev
// ---------------------------------------------------------------------------
static const Station* curStation() { return (ui.cur >= 0 && ui.cur < (int)stations.size()) ? &stations[ui.cur] : nullptr; }

static void splitTitle(const PlayerStatus& ps, const Station* s, String& l1, String& l2) {
    String t = ps.title;
    int dash = t.indexOf(" - ");
    if (dash > 0) { l1 = t.substring(0, dash); l2 = t.substring(dash + 3); }
    else if (t.length()) { l1 = t; l2 = s ? s->genre : ""; }
    else { l1 = ps.icyName.length() ? ps.icyName : (s ? s->name : ""); l2 = s ? s->genre : ""; }
}

// logotip poljubne velikosti (povečan/pomanjšan iz 64x64) ali ploščica z začetnico
static void drawLogo(const Station* s, int x, int y, int size) {
    const uint16_t* lg = s ? logoGet(s->url) : nullptr;
    if (lg) {
        if (size == LOGO_W) spr->pushImage(x, y, LOGO_W, LOGO_W, (const lgfx::swap565_t*)lg);
        else {
            float z = (float)size / LOGO_W;
            spr->setClipRect(x, y, size, size);
            spr->pushImageRotateZoom(x + size / 2.0f, y + size / 2.0f, LOGO_W / 2.0f, LOGO_W / 2.0f, 0, z, z, LOGO_W, LOGO_W, (const lgfx::swap565_t*)lg);
            spr->clearClipRect();
        }
        spr->drawRoundRect(x - 1, y - 1, size + 2, size + 2, 4, C_LINE);
    } else {
        spr->fillRoundRect(x, y, size, size, size / 8, C_HDR);
        String ini = s ? s->name : String("?");
        int cl = ini.length() && ((uint8_t)ini[0] >= 0xC0) ? 2 : 1;
        ini = ini.substring(0, cl); ini.toUpperCase();
        spr->setFont(size >= 90 ? &F_BIG : size >= 50 ? &F_TITLE : &F_HEAD); spr->setTextColor(C_HTXT);
        spr->setTextDatum(lgfx::textdatum_t::middle_center);
        spr->drawString(ini, x + size / 2, y + size / 2);
        spr->setTextDatum(lgfx::textdatum_t::baseline_left);
    }
}

// klasični VU (vodoravna stolpca) v poljubnem pravokotniku
static void drawVuBars(int x, int y, int w, int h, const PlayerStatus& ps, bool labels) {
    float tl = 0, tr = 0;
    auto curve = [](uint8_t v) { float f = min(1.0f, v / 127.0f); return f * f * (1.6f - 0.6f * f); };
    if (ps.playing) { tl = curve(ps.vu >> 8); tr = curve(ps.vu & 0xFF); }
    vuL = tl > vuL ? tl : vuL * 0.8f + tl * 0.2f;
    vuR = tr > vuR ? tr : vuR * 0.8f + tr * 0.2f;
    int lw = labels ? 16 : 0, bh = (h - 6) / 2;
    if (labels) {
        spr->setFont(&F_FOOT); spr->setTextColor(C_DIM);
        spr->drawString("L", x, y + bh / 2 + 5); spr->drawString("R", x, y + bh + 6 + bh / 2 + 5);
    }
    vuBar(x + lw, y, w - lw, bh, vuL, pkL, pkLt);
    vuBar(x + lw, y + bh + 6, w - lw, bh, vuR, pkR, pkRt);
}

// izbrani prikaz zvoka v pravokotniku (analogna merilnika le v klasični postavitvi -> drugje spekter)
static void drawViz(int x, int y, int w, int h, const PlayerStatus& ps, int bands = 24) {
    const String& v = cfg.viz;
    if (v == "scope") drawScope(x, y, w, h, ps);
    else if (v == "mirror") drawMirror(x, y, w, h, ps);
    else if (v == "vu") drawVuBars(x, y, w, h, ps, true);
    else drawSpectrum(x, y, w, h, ps, bands, h >= 100 ? 4 : 3);
}

static void drawVolLine(int x, int y, int w, int h) {
    spr->fillRoundRect(x, y, w, h, h / 2, C_LINE);
    int fw = ui.mute ? 0 : w * ui.vol / VOL_STEPS;
    if (fw > 0) spr->fillRoundRect(x, y, max(fw, h), h, h / 2, ui.mute ? C_DIM : C_ACC);
}

// besedilo stanja (kodek, povezovanje, opozorilo medpomnilnika, toast); vrne barvo
static String statusText(const PlayerStatus& ps, const char* toast, uint16_t& fc, bool longForm) {
    fc = C_SUB;
    if (toast) { fc = C_ACC; return toast; }
    if (ps.connecting) return "Povezujem ...";
    if (!ps.wanted) return "Ustavljeno";
    if (!ps.playing) return ps.reconnects ? "Ponovno povezujem ..." : "Povezujem ...";
    if (ps.bufferDs < 15) {
        char b[32]; snprintf(b, sizeof(b), "Medpomnilnik %u,%u s", ps.bufferDs / 10, ps.bufferDs % 10);
        fc = (millis() / 400) % 2 ? C_YEL : C_SUB; return b;
    }
    String c = ps.codec; c.toUpperCase();
    char b[48];
    if (ps.bitrate) snprintf(b, sizeof(b), "%s  %lu kb/s", c.c_str(), (unsigned long)(ps.bitrate / 1000));
    else snprintf(b, sizeof(b), "%s", c.c_str());
    String f = b;
    if (longForm && ps.sampleRate) { char sr[16]; snprintf(sr, sizeof(sr), "  %.1f kHz", ps.sampleRate / 1000.0f); f += sr; }
    if (ui.favOnly) f += "  *";
    return f;
}

static void drawClockAt(int rightX, int baseY, const lgfx::U8g2font* f, uint16_t col, uint16_t bg) {
    spr->setFont(f); spr->setTextColor(col);
    String clk = clockStr();
    drawClock(rightX - spr->textWidth(clk), baseY, clk, bg);
}

// skupno vsem postavitvam: luna časovnika in okno glasnosti
static void drawOverlays(int moonX, int moonY) {
    if (ui.sleepAt && moonX >= 0) {
        uint32_t left = (ui.sleepAt - millis() + 59999) / 60000;
        spr->fillCircle(moonX, moonY, 6, C_YEL); spr->fillCircle(moonX + 3, moonY - 2, 5, C_BG);
        spr->setFont(&F_FOOT); spr->setTextColor(C_YEL);
        char b[8]; snprintf(b, sizeof(b), "%lu'", (unsigned long)left);
        spr->drawString(b, moonX + 9, moonY + 6);
    }
    if (millis() < ui.volOverlayUntil) {
        int bx = 30, by = 104, bw = 260, bh = 66;
        spr->fillRoundRect(bx, by, bw, bh, 10, C_HDR);
        spr->drawRoundRect(bx, by, bw, bh, 10, C_ACC);
        drawSpeaker(bx + 14, by + 12, ui.mute, C_HTXT);
        spr->setFont(&F_TITLE); spr->setTextColor(C_HTXT);
        spr->drawString(ui.mute ? "Utišano" : "Glasnost", bx + 40, by + 26);
        char v[4]; snprintf(v, sizeof(v), "%d", ui.vol);
        spr->drawString(v, bx + bw - 14 - spr->textWidth(v), by + 26);
        int tx = bx + 14, tw = bw - 28, ty = by + 40;
        spr->fillRoundRect(tx, ty, tw, 14, 7, C_LINE);
        int fw = ui.mute ? 0 : tw * ui.vol / VOL_STEPS;
        if (fw > 0) spr->fillRoundRect(tx, ty, max(fw, 14), 14, 7, C_ACC);
    }
}

// ---------------------------------------------------------------------------
// A: klasična postavitev
// ---------------------------------------------------------------------------
static void layoutClassic(const PlayerStatus& ps, const char* toast) {
    const Station* s = curStation();
    spr->fillScreen(C_BG);
    spr->fillRect(0, 0, W, 38, C_HDR);
    spr->fillRoundRect(6, 7, 42, 24, 5, C_ACC);
    spr->setFont(&F_HEAD);
    char num[6]; snprintf(num, sizeof(num), "%02d", ui.cur + 1);
    spr->setTextColor(C_ONACC);
    spr->drawString(num, 27 - spr->textWidth(num) / 2, 25);
    spr->setTextColor(C_HTXT);
    mHead.set(s ? s->name : String("Ni postaj"));
    mHead.draw(56, 25, 216, 4, 34);
    drawWifi(284, 30);

    String l1, l2; splitTitle(ps, s, l1, l2);
    if (cfg.viz == "analog") {
        spr->setFont(&F_HEAD); spr->setTextColor(C_TXT);
        String one = l2.length() ? l1 + "  -  " + l2 : l1;
        mL1.set(one); mL1.draw(12, 58, 296, 40, 63);
        drawAnalog(ps);
    } else {
        int tx = 12, tw = 296;
        if (s && logoGet(s->url)) { drawLogo(s, 10, 44, LOGO_W); tx = 84; tw = 226; }
        spr->setFont(&F_TITLE); spr->setTextColor(C_TXT);
        mL1.set(l1); mL1.draw(tx, 74, tw, 50, 82);
        spr->setFont(&F_SUB); spr->setTextColor(C_SUB);
        mL2.set(l2); mL2.draw(tx, 104, tw, 86, 110);
        if (cfg.viz == "mirror") drawViz(14, 112, 296, 56, ps);
        else if (cfg.viz == "vu") drawVuBars(10, 124, 304, 38, ps, true);
        else drawViz(14, 114, 296, 52, ps);
    }
    drawSpeaker(10, 176, ui.mute, C_SUB);
    drawVolLine(34, 179, 236, 8);
    spr->setFont(&F_FOOT); spr->setTextColor(C_TXT);
    char v[4]; snprintf(v, sizeof(v), "%d", ui.vol);
    spr->drawString(v, 310 - spr->textWidth(v), 189);

    spr->drawFastHLine(0, 204, W, C_LINE);
    uint16_t fc; String foot = statusText(ps, toast, fc, true);
    spr->setFont(&F_FOOT); spr->setTextColor(fc); spr->drawString(foot, 10, 228);
    drawClockAt(310, 230, &F_HEAD, C_TXT, C_BG);
    drawOverlays(214, 222);
}

// ---------------------------------------------------------------------------
// B: kartica z velikim logotipom
// ---------------------------------------------------------------------------
static void layoutCard(const PlayerStatus& ps, const char* toast) {
    const Station* s = curStation();
    spr->fillScreen(C_BG);
    drawLogo(s, 12, 12, 120);
    char hd[64]; snprintf(hd, sizeof(hd), "%02d · %s", ui.cur + 1, s ? s->name.c_str() : "Ni postaj");
    spr->setFont(&F_FOOT); spr->setTextColor(C_ACC);
    mHead.set(hd); mHead.draw(146, 26, 166, 12, 30);
    drawWifi(290, 132);
    String l1, l2; splitTitle(ps, s, l1, l2);
    spr->setFont(&F_TITLE); spr->setTextColor(C_TXT);
    mL1.set(l1); mL1.draw(146, 62, 166, 38, 70);
    spr->setFont(&F_SUB); spr->setTextColor(C_SUB);
    mL2.set(l2); mL2.draw(146, 90, 166, 74, 96);
    uint16_t fc; String st = statusText(ps, toast, fc, false);
    spr->setFont(&F_SMALL); spr->setTextColor(fc);
    spr->setClipRect(146, 102, 140, 30); spr->drawString(st, 146, 120); spr->clearClipRect();

    drawViz(12, 142, 296, 44, ps, 32);

    drawVolLine(12, 198, 210, 6);
    spr->setFont(&F_FOOT); spr->setTextColor(C_SUB);
    char v[4]; snprintf(v, sizeof(v), "%d", ui.vol); spr->drawString(v, 230, 205);
    drawClockAt(310, 210, &F_HEAD, C_TXT, C_BG);
    // naslednja postaja
    if (s && stations.size() > 1) {
        int nx = (ui.cur + 1) % stations.size();
        String n = String("Naslednja: ") + stations[nx].name;
        spr->setFont(&F_SMALL); spr->setTextColor(C_DIM);
        spr->setClipRect(12, 220, 190, 20); spr->drawString(n, 12, 234); spr->clearClipRect();
    }
    drawOverlays(222, 228);
}

// ---------------------------------------------------------------------------
// C: radijska skala (kremna skala s kazalcem, ki potuje do izbrane postaje)
// ---------------------------------------------------------------------------
static float dialPos = -1;
static void layoutDial(const PlayerStatus& ps, const char* toast) {
    const Station* s = curStation();
    const uint16_t cream = lgfx::color565(236, 226, 196), ink = lgfx::color565(50, 35, 20),
                   red = lgfx::color565(200, 40, 30), brown = lgfx::color565(110, 85, 55), frame = lgfx::color565(90, 70, 40);
    spr->fillScreen(C_BG);
    spr->fillRoundRect(8, 8, 304, 98, 8, cream);
    spr->drawRoundRect(8, 8, 304, 98, 8, frame); spr->drawRoundRect(9, 9, 302, 96, 7, frame);
    int n = max<int>(1, stations.size());
    const int x0 = 24, x1 = 296;
    auto posX = [&](float i) { return n > 1 ? x0 + (x1 - x0) * i / (n - 1) : (x0 + x1) / 2; };
    int every = max(1, (n + 6) / 7);
    spr->setTextDatum(lgfx::textdatum_t::top_center);
    for (int i = 0; i < n; i++) {
        int x = posX(i);
        bool major = i % every == 0;
        spr->drawFastVLine(x, 22, major ? 14 : 7, ink);
        if (major) {
            char b[4]; snprintf(b, sizeof(b), "%02d", i + 1);
            spr->setFont(&F_TINY); spr->setTextColor(ink); spr->drawString(b, x, 40);
            if (i < (int)stations.size()) {
                String nm = stations[i].name;           // kratko ime: brez "Radio" na začetku
                String lo = nm; lo.toLowerCase();
                if (lo.startsWith("radio ") && nm.length() > 6) nm = nm.substring(6);
                nm.trim(); if (nm.length() > 6) nm = nm.substring(0, 6);
                spr->setTextColor(brown); spr->drawString(nm, x, 56);
            }
        }
    }
    spr->setTextDatum(lgfx::textdatum_t::baseline_left);
    spr->setFont(&F_TINY); spr->setTextColor(brown);
    spr->drawString("WEB RADIO", 18, 98); spr->drawString("ZRadio", 260, 98);
    // kazalec: mehko potuje do trenutne postaje
    float target = ui.cur;
    if (dialPos < 0) dialPos = target;
    dialPos += (target - dialPos) * 0.18f;
    if (fabsf(target - dialPos) < 0.01f) dialPos = target;
    int nxp = posX(dialPos);
    spr->fillRect(nxp - 1, 14, 3, 86, red);

    spr->setFont(&F_BIG); spr->setTextColor(C_ACC);
    mHead.set(s ? s->name : String("Ni postaj")); mHead.draw(14, 140, 292, 112, 146);
    String l1, l2; splitTitle(ps, s, l1, l2);
    String one = l2.length() && l1 != l2 ? l1 + " - " + l2 : l1;
    spr->setFont(&F_SUB); spr->setTextColor(C_SUB);
    mL1.set(one); mL1.draw(14, 166, 292, 150, 172);

    if (cfg.viz == "vu" || cfg.viz == "analog") drawVuBars(14, 178, 234, 22, ps, false);
    else drawViz(14, 176, 234, 26, ps, 24);
    drawClockAt(310, 200, &F_HEAD, C_TXT, C_BG);
    uint16_t fc; String st = statusText(ps, toast, fc, false);
    spr->setFont(&F_FOOT); spr->setTextColor(fc); spr->drawString(st, 14, 228);
    char v[8]; snprintf(v, sizeof(v), "%d", ui.vol);
    drawSpeaker(262, 216, ui.mute, C_SUB); spr->setTextColor(C_SUB); spr->drawString(v, 284, 228);
    drawOverlays(-1, 0);
}

// ---------------------------------------------------------------------------
// D: velika ura (za nočno omarico)
// ---------------------------------------------------------------------------
static void layoutClock(const PlayerStatus& ps, const char* toast) {
    const Station* s = curStation();
    spr->fillScreen(C_BG);
    spr->setFont(&F_CLOCK); spr->setTextColor(C_TXT);
    String c = clockStr();
    drawClock((W - spr->textWidth(c)) / 2, 76, c, C_BG);
    spr->setFont(&F_SUB); spr->setTextColor(C_SUB);
    center(dateStr(), 102);
    spr->drawFastHLine(12, 114, 296, C_LINE);
    drawLogo(s, 12, 122, 44);
    spr->setFont(&F_HEAD); spr->setTextColor(C_ACC);
    mHead.set(s ? s->name : String("Ni postaj")); mHead.draw(66, 140, 244, 122, 146);
    String l1, l2; splitTitle(ps, s, l1, l2);
    String one = l2.length() && l1 != l2 ? l1 + " - " + l2 : l1;
    spr->setFont(&F_SMALL); spr->setTextColor(C_TXT);
    mL1.set(one); mL1.draw(66, 162, 244, 148, 168);
    drawViz(12, 174, 296, 32, ps, 48);
    uint16_t fc; String st = statusText(ps, toast, fc, false);
    spr->setFont(&F_FOOT); spr->setTextColor(fc);
    char v[16]; snprintf(v, sizeof(v), "Glasnost %d", ui.vol);
    spr->drawString(toast ? st : String(v), 12, 228);
    if (!toast) { spr->setTextColor(fc); spr->drawString(st, 310 - spr->textWidth(st), 228); }
    drawOverlays(-1, 0);
}

// ---------------------------------------------------------------------------
// E: celozaslonski vizualizator
// ---------------------------------------------------------------------------
static void layoutFull(const PlayerStatus& ps, const char* toast) {
    const Station* s = curStation();
    spr->fillScreen(C_BG);
    if (cfg.viz == "vu" || cfg.viz == "analog") drawVuBars(6, 60, 308, 120, ps, true);
    else drawViz(6, 36, 308, 172, ps, 32);
    spr->setFont(&F_HEAD); spr->setTextColor(C_TXT);
    mHead.set(s ? s->name : String("Ni postaj")); mHead.draw(10, 24, 230, 4, 30);
    drawClockAt(310, 26, &F_HEAD, C_TXT, C_BG);
    String l1, l2; splitTitle(ps, s, l1, l2);
    String one = toast ? String(toast) : (l2.length() && l1 != l2 ? l1 + " - " + l2 : l1);
    spr->setFont(&F_SUB); spr->setTextColor(toast ? C_ACC : C_SUB);
    mL1.set(one); mL1.draw(10, 232, 300, 214, 238);
    drawOverlays(-1, 0);
}

// ---------------------------------------------------------------------------
// F: obroči (slog Volumio) – slika v ozadju, krožna kazalca, velik logotip
// ---------------------------------------------------------------------------
static LGFX_Sprite* bgSpr = nullptr;
static int          bgBuilt = -1;        // vrsta ozadja, ki je v bgSpr
static uint32_t     bgBuiltGen = 0;
static volatile uint32_t bgGen = 1;      // poveča se ob nalaganju lastne slike
void tftBgChanged() { bgGen = bgGen + 1; }

static uint16_t sw16(uint16_t v) { return (v >> 8) | (v << 8); }
// potemni pravokotnik ali krog v sliki (k = 0..256 delež ohranjene svetlosti) – "prosojna" podlaga
static void darken(int x0, int y0, int w, int h, int k, int cx = -1, int cy = -1, int r = 0) {
    uint16_t* b = (uint16_t*)spr->getBuffer();
    for (int y = max(0, y0); y < min(H, y0 + h); y++)
        for (int x = max(0, x0); x < min(W, x0 + w); x++) {
            if (r && (x - cx) * (x - cx) + (y - cy) * (y - cy) > r * r) continue;
            uint16_t v = sw16(b[y * W + x]);
            int R = (v >> 11) * k >> 8, G = ((v >> 5) & 63) * k >> 8, B = (v & 31) * k >> 8;
            b[y * W + x] = sw16((R << 11) | (G << 5) | B);
        }
}

static void buildBackground(int kind) {
    if (!bgSpr) {
        bgSpr = new LGFX_Sprite();
        bgSpr->setColorDepth(16); bgSpr->setPsram(true);
        if (!bgSpr->createSprite(W, H)) { delete bgSpr; bgSpr = nullptr; return; }
    }
    auto grad = [&](RGB a, RGB m, RGB z) {
        for (int y = 0; y < H; y++) {
            float t = (float)y / (H - 1); RGB p = t < 0.5f ? a : m, q = t < 0.5f ? m : z; float u = t < 0.5f ? t * 2 : (t - 0.5f) * 2;
            bgSpr->drawFastHLine(0, y, W, lgfx::color565(p.r + (q.r - p.r) * u, p.g + (q.g - p.g) * u, p.b + (q.b - p.b) * u));
        }
    };
    uint32_t seed = 12345;
    auto rnd = [&](int m) { seed = seed * 1103515245 + 12345; return (int)((seed >> 16) % m); };
    if (kind == 4) {                                       // lastna slika iz LittleFS
        File f = LittleFS.open("/bg.565", "r");
        if (f && f.size() == W * H * 2) { f.read((uint8_t*)bgSpr->getBuffer(), W * H * 2); f.close(); bgBuilt = kind; bgBuiltGen = bgGen; return; }
        if (f) f.close();
        kind = 0;                                          // ni slike -> nebo
    }
    if (kind == 1) {                                       // morje
        grad({4, 24, 40}, {0, 80, 100}, {6, 34, 46});
        for (int i = 0; i < 40; i++) { int y = 130 + rnd(110), x = rnd(W), l = 10 + rnd(40); bgSpr->drawFastHLine(x, y, l, lgfx::color565(40, 120, 140)); }
        bgSpr->fillCircle(250, 52, 18, lgfx::color565(230, 240, 220));
    } else if (kind == 2) {                                // sončni zahod
        grad({40, 20, 70}, {220, 90, 90}, {255, 180, 80});
        bgSpr->fillCircle(160, 150, 40, lgfx::color565(255, 220, 120));
        for (int i = 0; i < 6; i++) bgSpr->fillRect(0, 150 + i * 9, W, 3 + i, lgfx::color565(220, 90, 90));
        bgSpr->fillRect(0, 196, W, 44, lgfx::color565(50, 25, 60));
    } else if (kind == 3) {                                // barve teme
        const Theme& t = THEMES[curTheme < 0 ? 0 : curTheme];
        grad(t.hdr, t.bg, t.bg);
    } else {                                               // nebo z zvezdami in griči
        grad({20, 30, 70}, {150, 80, 140}, {250, 160, 90});
        for (int i = 0; i < 70; i++) bgSpr->drawPixel(rnd(W), rnd(110), lgfx::color565(230, 230, 255));
        int pts[][2] = {{0, 200}, {60, 175}, {120, 190}, {190, 165}, {260, 185}, {320, 170}};
        for (int i = 0; i < 5; i++) {
            bgSpr->fillTriangle(pts[i][0], pts[i][1], pts[i + 1][0], pts[i + 1][1], pts[i][0], H, lgfx::color565(40, 30, 60));
            bgSpr->fillTriangle(pts[i + 1][0], pts[i + 1][1], pts[i + 1][0], H, pts[i][0], H, lgfx::color565(40, 30, 60));
        }
    }
    bgBuilt = kind; bgBuiltGen = bgGen;
}

static uint32_t songSeq = 0, songStart = 0, stationStart = 0;
static String   stationUrl;

static void ring(int cx, int cy, int r, float frac, uint16_t col) {
    darken(cx - r, cy - r, 2 * r + 1, 2 * r + 1, 110, cx, cy, r);
    spr->fillArc(cx, cy, r - 7, r, 0, 360, lgfx::color565(70, 70, 80));
    if (frac > 0.002f) spr->fillArc(cx, cy, r - 7, r, -90, -90 + 360 * min(1.0f, frac), col);
}

static void layoutRings(const PlayerStatus& ps, const char* toast) {
    const Station* s = curStation();
    int want = cfg.bg_kind;
    if (want == 3 && bgBuilt == 3) { /* tema se lahko zamenja */ static int lastTh = -1; if (lastTh != curTheme) { lastTh = curTheme; bgBuilt = -1; } }
    if (!bgSpr || bgBuilt != want || bgBuiltGen != bgGen) buildBackground(want);
    if (bgSpr) memcpy(spr->getBuffer(), bgSpr->getBuffer(), W * H * 2); else spr->fillScreen(C_BG);

    // sledenje času pesmi in postaje
    uint32_t seq = playerTitleSeq();
    if (seq != songSeq) { songSeq = seq; songStart = millis(); }
    if (ps.url != stationUrl) { stationUrl = ps.url; stationStart = millis(); }

    const uint16_t white = lgfx::color565(255, 255, 255), soft = lgfx::color565(225, 225, 235);
    // zgornji pas
    darken(0, 0, W, 28, 150);
    drawWifi(8, 22);
    spr->setFont(&F_FOOT);
    uint16_t fc; String st = statusText(ps, toast, fc, false);
    bool showClockTop = !(cfg.ring_mode == 1 && !ui.sleepAt);
    if (toast || !ps.playing || ps.bufferDs < 15) { spr->setTextColor(toast ? C_ACC : (fc == C_YEL ? C_YEL : soft)); spr->setClipRect(44, 0, 216, 28); spr->drawString(st, 44, 19); spr->clearClipRect(); }
    else { char n[8]; snprintf(n, sizeof(n), "%02d", ui.cur + 1); spr->setTextColor(soft); spr->drawString(n, 44, 19);
           spr->fillTriangle(156, 8, 156, 20, 166, 14, C_ACC); }
    if (showClockTop) {                                    // brez utripanja: podlaga je slika
        spr->setFont(&F_FOOT); spr->setTextColor(white);
        String clk = clockStr(); spr->drawString(clk, 312 - spr->textWidth(clk), 21);
    }

    // levi obroč
    const int lx = 62, ry = 106, rr = 46;
    float frac = 0; String big, l1, l2;
    char b[24];
    String c = ps.codec; c.toUpperCase();
    if (ps.bitrate) snprintf(b, sizeof(b), "%s %luk", c.c_str(), (unsigned long)(ps.bitrate / 1000)); else snprintf(b, sizeof(b), "%s", c.c_str());
    l1 = b;
    if (ps.sampleRate) { snprintf(b, sizeof(b), "%.1f kHz", ps.sampleRate / 1000.0f); l2 = b; }
    if (ui.sleepAt) {                                      // časovnik izklopa ima prednost
        uint32_t left = (ui.sleepAt - millis()) / 1000;
        snprintf(b, sizeof(b), "%lu:%02lu", (unsigned long)(left / 60), (unsigned long)(left % 60)); big = b;
        frac = left / (90.0f * 60); l1 = "izklop"; l2 = "";
    } else if (cfg.ring_mode == 1 && appTimeValid()) {     // ura, obroč = sekunde
        time_t now = time(nullptr); struct tm tm; localtime_r(&now, &tm);
        snprintf(b, sizeof(b), "%02d:%02d", tm.tm_hour, tm.tm_min); big = b; frac = tm.tm_sec / 60.0f;
    } else if (cfg.ring_mode == 2) {                       // čas poslušanja postaje
        uint32_t t = (millis() - stationStart) / 1000;
        if (t < 3600) snprintf(b, sizeof(b), "%lu:%02lu", (unsigned long)(t / 60), (unsigned long)(t % 60));
        else snprintf(b, sizeof(b), "%lu:%02lu h", (unsigned long)(t / 3600), (unsigned long)(t / 60 % 60));
        big = b; frac = (t % 3600) / 3600.0f;
    } else if (cfg.ring_mode == 3) {                       // medpomnilnik
        snprintf(b, sizeof(b), "%u,%u s", ps.bufferDs / 10, ps.bufferDs % 10); big = b; frac = ps.bufferPct / 100.0f;
    } else {                                               // čas pesmi (obroč ~ 4 min)
        uint32_t t = ps.playing ? (millis() - songStart) / 1000 : 0;
        snprintf(b, sizeof(b), "%lu:%02lu", (unsigned long)(t / 60), (unsigned long)(t % 60)); big = b; frac = t / 240.0f;
    }
    ring(lx, ry, rr, frac, C_ACC);
    spr->setTextDatum(lgfx::textdatum_t::middle_center);
    spr->setFont(&F_HEAD); spr->setTextColor(white); spr->drawString(big, lx, ry - 8);
    spr->setFont(&F_TINY); spr->setTextColor(soft); spr->drawString(l1, lx, ry + 10); spr->drawString(l2, lx, ry + 21);

    // desni obroč: glasnost
    const int vx = 258;
    ring(vx, ry, rr, ui.mute ? 0 : (float)ui.vol / VOL_STEPS, C_ACC);
    spr->setFont(&F_BIG); spr->setTextColor(white);
    snprintf(b, sizeof(b), "%d", ui.vol); spr->drawString(ui.mute ? "-" : b, vx, ry - 4);
    spr->setFont(&F_TINY); spr->setTextColor(soft); spr->drawString(ui.mute ? "utišano" : "/30", vx, ry + 20);
    spr->setTextDatum(lgfx::textdatum_t::baseline_left);

    // logotip na sredini
    drawLogo(s, 112, 58, 96);

    // spodnji pas
    darken(0, 162, W, 78, 110);
    spr->setFont(&F_FOOT); spr->setTextColor(C_ACC);
    mHead.set(s ? s->name : String("Ni postaj")); mHead.draw(12, 178, 180, 164, 182);
    String a1, a2; splitTitle(ps, s, a1, a2);
    spr->setFont(&F_TITLE); spr->setTextColor(white);
    mL1.set(a1); mL1.draw(12, 204, 184, 184, 210);
    spr->setFont(&F_SUB); spr->setTextColor(soft);
    mL2.set(a2); mL2.draw(12, 228, 184, 212, 234);
    // mali spekter desno spodaj (le prižgani stolpci, brez temnih segmentov)
    const int nb = 18, bx0 = 204, bw = 6, by1 = 232, bh = 50;
    spectrumCompute(spB, nb, ps.sampleRate);
    for (int i = 0; i < nb; i++) {
        int h = ps.playing ? (int)(spB[i] * bh) : 0;
        if (h < 2) h = 2;
        spr->fillRect(bx0 + i * bw, by1 - h, bw - 2, h, C_ACC);
    }
    drawOverlays(-1, 0);
}


// ---------------------------------------------------------------------------
// Skupno novim postavitvam
// ---------------------------------------------------------------------------
static inline uint16_t c565(int r, int g, int b) { return lgfx::color565(r, g, b); }

// obarvaj pravokotnik proti barvi (k = 0..256), po želji z zaobljenimi vogali – "steklo"
static void tint(int x0, int y0, int w, int h, int k, RGB c, int rad = 0) {
    uint16_t* b = (uint16_t*)spr->getBuffer();
    const int tr = c.r >> 3, tg = c.g >> 2, tb = c.b >> 3;
    for (int y = max(0, y0); y < min(H, y0 + h); y++)
        for (int x = max(0, x0); x < min(W, x0 + w); x++) {
            if (rad) {
                int dx = x < x0 + rad ? x0 + rad - x : (x >= x0 + w - rad ? x - (x0 + w - rad - 1) : 0);
                int dy = y < y0 + rad ? y0 + rad - y : (y >= y0 + h - rad ? y - (y0 + h - rad - 1) : 0);
                if (dx && dy && dx * dx + dy * dy > rad * rad) continue;
            }
            uint16_t v = sw16(b[y * W + x]);
            int R = v >> 11, G = (v >> 5) & 63, B = v & 31;
            R += (tr - R) * k / 256; G += (tg - G) * k / 256; B += (tb - B) * k / 256;
            b[y * W + x] = sw16((R << 11) | (G << 5) | B);
        }
}

// drseče besedilo s senco/sijem (sence ne premika dvakrat)
static void marqGlow(TMarq& m, int x, int y, int w, int top, int bot, uint16_t glow, uint16_t col, int dx, int dy) {
    int o = m.off; uint32_t p = m.pause;
    spr->setTextColor(glow); m.draw(x + dx, y + dy, w, top, bot + dy);
    m.off = o; m.pause = p;
    spr->setTextColor(col); m.draw(x, y, w, top, bot);
}
// na sredino, če gre v okno, sicer drsi
static void marqCenter(TMarq& m, const String& t, int cx, int y, int w, int top, int bot) {
    m.set(t);
    int tw = spr->textWidth(t);
    m.draw(tw <= w ? cx - tw / 2 : cx - w / 2, y, w, top, bot);
}

static void vuTrack(const PlayerStatus& ps) {
    float tl = 0, tr = 0;
    auto curve = [](uint8_t v) { float f = min(1.0f, v / 127.0f); return f * f * (1.6f - 0.6f * f); };
    if (ps.playing) { tl = curve(ps.vu >> 8); tr = curve(ps.vu & 0xFF); }
    vuL = tl > vuL ? tl : vuL * 0.8f + tl * 0.2f;
    vuR = tr > vuR ? tr : vuR * 0.8f + tr * 0.2f;
}

static uint32_t frameDt() {                    // ms od prejšnje slike (za vrtenje)
    static uint32_t last = 0;
    uint32_t now = millis(), dt = last ? now - last : 0;
    last = now;
    return dt > 200 ? 200 : dt;
}

static void songTrack(const PlayerStatus& ps) {
    uint32_t seq = playerTitleSeq();
    if (seq != songSeq) { songSeq = seq; songStart = millis(); }
    if (ps.url != stationUrl) { stationUrl = ps.url; stationStart = millis(); }
}

static void footVolume(int x, int y, uint16_t col) {   // zvočnik + številka, y = osnovnica
    drawSpeaker(x, y - 12, ui.mute, col);
    spr->setFont(&F_FOOT); spr->setTextColor(col);
    char v[6]; snprintf(v, sizeof(v), "%d", ui.vol); spr->drawString(v, x + 22, y);
}

// ---------------------------------------------------------------------------
// G: gramofon – vrteča se plošča z logotipom na etiketi in ročica
// ---------------------------------------------------------------------------
static float vinAng = 0, armAng = 4;
static void layoutVinyl(const PlayerStatus& ps, const char* toast) {
    const Station* s = curStation();
    uint32_t dt = frameDt();
    spr->fillScreen(C_BG);
    const int cx = 96, cy = 108, R = 90, L = 34;
    float target = ps.playing ? -17.0f : 4.0f;          // ročica na plošči / na stojalu
    armAng += (target - armAng) * 0.12f;
    if (ps.playing && armAng < -14.0f) vinAng = fmodf(vinAng + dt * 0.2f, 360.0f);   // 33 1/3 obr/min

    spr->fillCircle(cx, cy, R + 6, c565(64, 64, 70));
    spr->drawCircle(cx, cy, R + 6, c565(110, 110, 118));
    const uint16_t* lg = s ? logoGet(s->url) : nullptr;
    if (lg) {
        float z = 2.0f * L / LOGO_W + 0.02f;
        spr->pushImageRotateZoom(cx, cy, LOGO_W / 2.0f, LOGO_W / 2.0f, vinAng, z, z, LOGO_W, LOGO_W, (const lgfx::swap565_t*)lg);
    } else {
        spr->fillCircle(cx, cy, L, C_ACC);
        String ini = s ? s->name : String("?");
        int cl = ini.length() && ((uint8_t)ini[0] >= 0xC0) ? 2 : 1;
        ini = ini.substring(0, cl); ini.toUpperCase();
        spr->setFont(&F_TITLE); spr->setTextColor(C_ONACC);
        spr->setTextDatum(lgfx::textdatum_t::middle_center); spr->drawString(ini, cx, cy); spr->setTextDatum(lgfx::textdatum_t::baseline_left);
        float a = vinAng * DEG_TO_RAD;
        spr->fillCircle(cx + (L - 7) * cosf(a), cy + (L - 7) * sinf(a), 3, C_ONACC);
    }
    // vinil (prekrije tudi vogale zasukane slike)
    spr->fillArc(cx, cy, L, R, 0, 360, c565(26, 26, 30));
    spr->fillArc(cx, cy, L + 8, R - 3, -62, -34, c565(52, 52, 58));      // odsev luči
    spr->fillArc(cx, cy, L + 8, R - 3, 118, 146, c565(44, 44, 50));
    for (int r = L + 6; r < R - 2; r += 3) spr->drawCircle(cx, cy, r, c565(14, 14, 16));
    spr->drawCircle(cx, cy, R - 1, c565(60, 60, 66));
    spr->drawCircle(cx, cy, L, c565(12, 12, 12));
    spr->fillCircle(cx, cy, 3, c565(215, 215, 215));

    // ročica
    const float px = 196, py = 26, len = 128;
    float a = armAng * DEG_TO_RAD, sa = sinf(a), ca = cosf(a);
    float ex = px + len * sa, ey = py + len * ca;
    spr->fillCircle(206, 150, 5, c565(50, 50, 56));                     // stojalo
    spr->fillCircle(px, py, 15, c565(56, 56, 62));
    spr->fillCircle(px - 17 * sa, py - 17 * ca, 8, c565(150, 150, 156)); // protiutež
    spr->drawWideLine(px, py, ex, ey, 2.2f, c565(205, 205, 210));
    spr->drawWideLine(ex - 16 * sa, ey - 16 * ca, ex, ey, 4.5f, c565(125, 125, 132));
    spr->fillCircle(px, py, 6, c565(185, 185, 190));

    // desni stolpec
    const int tx = 218, tw = 94;
    String l1, l2; splitTitle(ps, s, l1, l2);
    spr->setFont(&F_FOOT); spr->setTextColor(C_ACC);
    mHead.set(s ? s->name : String("Ni postaj")); mHead.draw(tx, 20, tw, 6, 24);
    spr->setFont(&F_HEAD); spr->setTextColor(C_TXT);
    mL1.set(l1); mL1.draw(tx, 50, tw, 34, 56);
    spr->setFont(&F_SMALL); spr->setTextColor(C_SUB);
    mL2.set(l2); mL2.draw(tx, 70, tw, 58, 74);
    uint16_t fc; String st = statusText(ps, toast, fc, false);
    spr->setFont(&F_TINY); spr->setTextColor(fc);
    spr->setClipRect(tx, 78, tw, 14); spr->drawString(st, tx, 89); spr->clearClipRect();
    drawSpectrum(tx, 100, tw, 80, ps, 14, 3);

    // spodaj
    drawSpeaker(12, 216, ui.mute, C_SUB);
    drawVolLine(34, 221, 168, 6);
    spr->setFont(&F_FOOT); spr->setTextColor(C_SUB);
    char v[4]; snprintf(v, sizeof(v), "%d", ui.vol); spr->drawString(v, 210, 229);
    drawClockAt(310, 232, &F_HEAD, C_TXT, C_BG);
    drawOverlays(224, 196);
}

// ---------------------------------------------------------------------------
// H: kaseta – koluta se vrtita, trak se previja s časom pesmi
// ---------------------------------------------------------------------------
static float reelA[2] = {0, 0};
static void reel(int cx, int cy, int r, float ang) {
    spr->fillCircle(cx, cy, r, c565(72, 46, 30));
    spr->drawCircle(cx, cy, r, c565(100, 66, 42));
    if (r > 14) spr->drawCircle(cx, cy, r - 5, c565(84, 54, 35));
    spr->fillCircle(cx, cy, 9, c565(228, 228, 222));
    spr->fillCircle(cx, cy, 5, c565(24, 22, 22));
    for (int k = 0; k < 6; k++) {
        float a = (ang + k * 60) * DEG_TO_RAD;
        spr->drawWideLine(cx + 2.5f * cosf(a), cy + 2.5f * sinf(a), cx + 6 * cosf(a), cy + 6 * sinf(a), 1.0f, c565(228, 228, 222));
    }
}
static void layoutCassette(const PlayerStatus& ps, const char* toast) {
    const Station* s = curStation();
    uint32_t dt = frameDt();
    songTrack(ps);
    spr->fillScreen(C_BG);
    const uint16_t body = c565(46, 46, 52), edge = c565(86, 86, 94), paper = c565(236, 230, 214), ink = c565(30, 30, 60);
    spr->fillRoundRect(16, 8, 288, 180, 12, body);
    spr->drawRoundRect(16, 8, 288, 180, 12, edge);
    const int sc[4][2] = {{26, 18}, {294, 18}, {26, 178}, {294, 178}};
    for (auto& p : sc) { spr->fillCircle(p[0], p[1], 3, c565(120, 120, 126)); spr->drawFastHLine(p[0] - 2, p[1], 5, c565(60, 60, 64)); }
    // etiketa
    spr->fillRoundRect(30, 18, 260, 126, 6, paper);
    spr->fillRect(30, 50, 260, 5, C_ACC);
    spr->fillRect(30, 57, 260, 2, C_SUB);
    spr->setFont(&F_HEAD); spr->setTextColor(ink);
    mHead.set(s ? s->name : String("Ni postaj")); mHead.draw(40, 40, 214, 24, 46);
    spr->setFont(&F_BIG); spr->setTextColor(C_ACC); spr->drawString("A", 264, 44);
    // okno s kolutoma
    spr->fillRoundRect(86, 66, 148, 56, 8, c565(24, 22, 22));
    spr->drawRoundRect(86, 66, 148, 56, 8, c565(110, 110, 110));
    uint32_t t = ps.playing ? (millis() - songStart) / 1000 : 0;
    float p = (t % 240) / 240.0f;
    int rL = 9 + (int)(15 * (1 - p)), rR = 9 + (int)(15 * p);
    if (ps.playing) { reelA[0] = fmodf(reelA[0] + dt * 1.6f / rL, 360); reelA[1] = fmodf(reelA[1] + dt * 1.6f / rR, 360); }
    reel(122, 94, rL, reelA[0]);
    reel(198, 94, rR, reelA[1]);
    char b[24];
    spr->setFont(&F_FOOT); spr->setTextColor(ink);
    snprintf(b, sizeof(b), "%lu:%02lu", (unsigned long)(t / 60), (unsigned long)(t % 60)); spr->drawString(b, 38, 100);
    if (ps.bitrate) { snprintf(b, sizeof(b), "%luk", (unsigned long)(ps.bitrate / 1000)); spr->drawString(b, 282 - spr->textWidth(b), 100); }
    String l1, l2; splitTitle(ps, s, l1, l2);
    String one = l2.length() && l1 != l2 ? l1 + " - " + l2 : l1;
    spr->setFont(&F_SMALL); spr->setTextColor(ink);
    mL1.set(one); mL1.draw(40, 138, 240, 126, 142);
    // spodnji trapez z luknjami
    const uint16_t trap = c565(62, 62, 68);
    spr->fillRect(76, 154, 168, 34, trap);
    spr->fillTriangle(76, 154, 76, 188, 60, 188, trap);
    spr->fillTriangle(244, 154, 244, 188, 260, 188, trap);
    spr->fillCircle(110, 172, 5, c565(20, 20, 22)); spr->fillCircle(210, 172, 5, c565(20, 20, 22));
    spr->fillRect(146, 170, 8, 8, c565(20, 20, 22)); spr->fillRect(166, 170, 8, 8, c565(20, 20, 22));

    // pod kaseto
    drawVuBars(12, 196, 196, 24, ps, true);
    drawClockAt(310, 216, &F_HEAD, C_TXT, C_BG);
    uint16_t fc; String st = statusText(ps, toast, fc, false);
    spr->setFont(&F_FOOT); spr->setTextColor(fc);
    spr->setClipRect(12, 224, 230, 16); spr->drawString(st, 12, 236); spr->clearClipRect();
    footVolume(262, 236, C_SUB);
    drawOverlays(-1, 0);
}

// ---------------------------------------------------------------------------
// I: Sintwave 80' – neonsko nebo, črtasto sonce, premikajoča mreža, cian spekter
// ---------------------------------------------------------------------------
static bool ensureBg() {
    if (bgSpr) return true;
    bgSpr = new LGFX_Sprite(); bgSpr->setColorDepth(16); bgSpr->setPsram(true);
    if (!bgSpr->createSprite(W, H)) { delete bgSpr; bgSpr = nullptr; return false; }
    return true;
}
static constexpr int HOR = 130;
static void buildSynth() {
    if (!ensureBg()) return;
    auto lerp = [](RGB a, RGB b, float t) { return RGB{(uint8_t)(a.r + (b.r - a.r) * t), (uint8_t)(a.g + (b.g - a.g) * t), (uint8_t)(a.b + (b.b - a.b) * t)}; };
    auto sky = [&](int y) { float t = (float)y / (HOR - 1); return t < 0.6f ? lerp({10, 4, 36}, {70, 14, 100}, t / 0.6f) : lerp({70, 14, 100}, {205, 50, 140}, (t - 0.6f) / 0.4f); };
    for (int y = 0; y < HOR; y++) { RGB c = sky(y); bgSpr->drawFastHLine(0, y, W, c565(c.r, c.g, c.b)); }
    uint32_t seed = 777;
    auto rnd = [&](int m) { seed = seed * 1103515245 + 12345; return (int)((seed >> 16) % m); };
    for (int i = 0; i < 45; i++) bgSpr->drawPixel(rnd(W), rnd(70), c565(220, 210, 255));
    // sonce s prečnimi režami v spodnji polovici
    const int sx = 160, sy = 106, sr = 50;
    for (int y = sy - sr; y < HOR; y++) {
        int dy = y - sy; if (dy * dy > sr * sr) continue;
        int rel = y - (sy - 8);
        if (rel >= 0 && rel % 9 < 1 + rel / 9) continue;
        int hw = (int)sqrtf((float)(sr * sr - dy * dy));
        RGB c = lerp({255, 230, 100}, {255, 60, 160}, (float)(y - (sy - sr)) / (HOR - (sy - sr)));
        bgSpr->drawFastHLine(sx - hw, y, 2 * hw + 1, c565(c.r, c.g, c.b));
    }
    // gore v ozadju
    const int mt[][2] = {{0, 118}, {30, 100}, {62, 116}, {96, 92}, {124, 120}, {200, 120}, {226, 96}, {256, 114}, {292, 98}, {320, 116}};
    for (int i = 0; i < 9; i++) {
        if (mt[i][0] >= 124 && mt[i + 1][0] <= 200) continue;
        bgSpr->fillTriangle(mt[i][0], mt[i][1], mt[i + 1][0], mt[i + 1][1], mt[i][0], HOR, c565(40, 8, 60));
        bgSpr->fillTriangle(mt[i + 1][0], mt[i + 1][1], mt[i + 1][0], HOR, mt[i][0], HOR, c565(40, 8, 60));
        bgSpr->drawLine(mt[i][0], mt[i][1], mt[i + 1][0], mt[i + 1][1], c565(150, 40, 170));
    }
    for (int y = HOR; y < H; y++) { RGB c = lerp({34, 4, 54}, {8, 0, 20}, (float)(y - HOR) / (H - HOR)); bgSpr->drawFastHLine(0, y, W, c565(c.r, c.g, c.b)); }
    bgSpr->drawFastHLine(0, HOR - 1, W, c565(170, 60, 170));
    bgSpr->drawFastHLine(0, HOR, W, c565(255, 110, 230));
    bgBuilt = 100;
}
static float gridPh = 0;
static void layoutSynth(const PlayerStatus& ps, const char* toast) {
    const Station* s = curStation();
    uint32_t dt = frameDt();
    if (!bgSpr || bgBuilt != 100) buildSynth();
    if (bgSpr) memcpy(spr->getBuffer(), bgSpr->getBuffer(), W * H * 2); else spr->fillScreen(C_BG);
    if (ps.playing) gridPh = fmodf(gridPh + dt / 900.0f, 1.0f);
    // mreža: vodoravne črte se bližajo
    for (int k = 1; k < 18; k++) {
        float z = k - gridPh;
        int y = HOR + (int)(220.0f / (z + 2.0f));
        if (y <= HOR + 1 || y >= H) continue;
        float f = 2.0f / (z + 2.0f);
        spr->drawFastHLine(0, y, W, c565(60 + 195 * f, 10 + 30 * f, 90 + 130 * f));
    }
    for (int i = -12; i <= 12; i++) spr->drawLine(160 + i * 6, HOR + 1, 160 + i * 44, H, c565(190, 40, 170));
    // spekter na obzorju z odsevom
    const int nb = 32, step = 9, x0 = (W - nb * step) / 2 + 1;
    spectrumCompute(spB, nb, ps.sampleRate);
    for (int i = 0; i < nb; i++) {
        int h = ps.playing ? (int)(spB[i] * 58) : 0;
        if (h < 2) h = 2;
        int x = x0 + i * step;
        spr->fillRect(x, HOR - 1 - h, 6, h, c565(0, 225, 255));
        spr->fillRect(x, HOR - 1 - h, 6, 2, c565(210, 255, 255));
        spr->fillRect(x, HOR + 2, 6, h / 3, c565(0, 80, 115));
    }
    // ime postaje z neonskim sijem
    spr->setFont(&F_BIG);
    String nm = s ? s->name : String("Ni postaj");
    mHead.set(nm);
    int tw = spr->textWidth(nm), nx = tw <= 290 ? (W - tw) / 2 : 15;
    marqGlow(mHead, nx, 44, 290, 20, 50, c565(110, 10, 140), c565(255, 90, 215), 2, 2);
    spr->setFont(&F_FOOT); spr->setTextColor(c565(120, 240, 255));
    String clk = clockStr(); spr->drawString(clk, 312 - spr->textWidth(clk), 14);
    char n[6]; snprintf(n, sizeof(n), "%02d", ui.cur + 1); spr->drawString(n, 8, 14);
    // spodaj
    darken(0, 198, W, 42, 92);
    String l1, l2; splitTitle(ps, s, l1, l2);
    spr->setFont(&F_HEAD); spr->setTextColor(c565(255, 255, 255));
    mL1.set(l1); mL1.draw(10, 217, 300, 200, 222);
    uint16_t fc; String st = statusText(ps, toast, fc, false);
    bool showSt = toast || !ps.playing || ps.bufferDs < 15;
    spr->setFont(&F_SMALL); spr->setTextColor(showSt ? (fc == C_SUB ? c565(120, 240, 255) : fc) : c565(120, 240, 255));
    String low = showSt ? st : l2;
    spr->setClipRect(10, 224, 250, 16); spr->drawString(low, 10, 236); spr->clearClipRect();
    char v[6]; snprintf(v, sizeof(v), "%d", ui.vol);
    spr->setTextColor(c565(255, 90, 215)); spr->drawString(ui.mute ? "-" : v, 310 - spr->textWidth(ui.mute ? "-" : v), 236);
    drawOverlays(-1, 0);
}

// ---------------------------------------------------------------------------
// J: ura z nixie cevmi + VFD prikazovalnik
// ---------------------------------------------------------------------------
static void nixieTube(int x, int y, char d) {
    const int w = 60, h = 94;
    spr->fillRoundRect(x, y, w, h, 14, c565(22, 17, 14));
    spr->setClipRect(x + 4, y + 6, w - 8, h - 12);
    for (int i = x + 6; i < x + w - 4; i += 5) spr->drawFastVLine(i, y + 6, h - 12, c565(36, 27, 21));
    for (int j = y + 8; j < y + h - 6; j += 5) spr->drawFastHLine(x + 4, j, w - 8, c565(36, 27, 21));
    spr->clearClipRect();
    spr->setFont(&F_CLOCK);
    char g[2] = {'8', 0};
    int gx = x + (w - spr->textWidth(g)) / 2;
    spr->setTextColor(c565(48, 30, 18)); spr->drawString(g, gx, y + 80);           // ugasnjene katode
    g[0] = d;
    int dx = x + (w - spr->textWidth(g)) / 2;
    spr->setTextColor(c565(105, 38, 6));
    spr->drawString(g, dx - 2, y + 80); spr->drawString(g, dx + 2, y + 80);
    spr->drawString(g, dx, y + 78); spr->drawString(g, dx, y + 82);
    spr->setTextColor(c565(255, 155, 55)); spr->drawString(g, dx, y + 80);
    spr->drawRoundRect(x, y, w, h, 14, c565(80, 68, 56));
    spr->drawFastVLine(x + 7, y + 14, 58, c565(70, 64, 58));                      // odsev stekla
    spr->fillRect(x + 6, y + h, w - 12, 8, c565(40, 40, 44));                     // podnožje
}
static void layoutNixie(const PlayerStatus& ps, const char* toast) {
    const Station* s = curStation();
    String c = clockStr();                                // "HH:MM" ali "--:--"
    const uint16_t V = c565(90, 255, 215), Vd = c565(12, 46, 38);
    static String nixStr;
    if (!bgSpr || bgBuilt != 102 || c != nixStr) {        // statični del le ob spremembi minute
        spr->fillScreen(c565(8, 6, 5));
        const int xs[4] = {12, 80, 180, 248};
        for (int i = 0; i < 4; i++) nixieTube(xs[i], 8, c[i < 2 ? i : i + 1]);
        spr->fillRoundRect(8, 116, 304, 92, 6, c565(2, 12, 10));
        spr->drawRoundRect(8, 116, 304, 92, 6, c565(30, 70, 60));
        for (int y = 119; y < 206; y += 3) for (int x = 11; x < 309; x += 3) spr->drawPixel(x, y, c565(6, 22, 19));
        if (ensureBg()) { memcpy(bgSpr->getBuffer(), spr->getBuffer(), W * H * 2); bgBuilt = 102; nixStr = c; }
    } else memcpy(spr->getBuffer(), bgSpr->getBuffer(), W * H * 2);
    bool on = !appTimeValid() || (millis() / 500) % 2 == 0;
    for (int k = 0; k < 2; k++) {
        int cy = 8 + 34 + k * 28;
        spr->fillCircle(160, cy, 6, on ? c565(105, 38, 6) : c565(30, 18, 10));
        spr->fillCircle(160, cy, 4, on ? c565(255, 155, 55) : c565(48, 30, 18));
    }
    spr->setFont(&F_HEAD); spr->setTextColor(V);
    mHead.set(s ? s->name : String("Ni postaj")); mHead.draw(18, 138, 284, 122, 142);
    String l1, l2; splitTitle(ps, s, l1, l2);
    String one = l2.length() && l1 != l2 ? l1 + " - " + l2 : l1;
    spr->setFont(&F_SMALL); spr->setTextColor(V);
    mL1.set(one); mL1.draw(18, 158, 284, 146, 162);
    vuTrack(ps);
    spr->setFont(&F_TINY);
    for (int ch = 0; ch < 2; ch++) {
        int y = 170 + ch * 16, lit = (int)((ch ? vuR : vuL) * 44 + 0.5f);
        spr->setTextColor(V); spr->drawString(ch ? "R" : "L", 18, y + 8);
        for (int i = 0; i < 44; i++) spr->fillRect(30 + i * 6, y, 5, 9, i < lit ? (i >= 38 ? c565(255, 120, 90) : V) : Vd);
    }
    uint16_t fc; String st = statusText(ps, toast, fc, false);
    spr->setFont(&F_FOOT); spr->setTextColor(fc == C_SUB ? c565(200, 120, 50) : fc);
    spr->setClipRect(10, 220, 230, 20); spr->drawString(st, 10, 233); spr->clearClipRect();
    footVolume(262, 233, c565(200, 120, 50));
    drawOverlays(-1, 0);
}

// ---------------------------------------------------------------------------
// K: krožni spekter okoli logotipa (nizki toni zgoraj, zrcaljeno levo/desno)
// ---------------------------------------------------------------------------
static void layoutRadial(const PlayerStatus& ps, const char* toast) {
    const Station* s = curStation();
    spr->fillScreen(C_BG);
    const int cx = 160, cy = 104, NB = 40;
    const float da = 180.0f / NB;
    spectrumCompute(spB, NB, ps.sampleRate);
    float bass = ps.playing ? (spB[0] + spB[1] + spB[2]) / 3 : 0;
    // logotip v krogu, rahlo utripa z nizkimi toni
    int lr = 42 + (int)(4 * bass);
    const uint16_t* lg = s ? logoGet(s->url) : nullptr;
    if (lg) {
        float z = 2.0f * lr / LOGO_W + 0.02f;
        spr->pushImageRotateZoom(cx, cy, LOGO_W / 2.0f, LOGO_W / 2.0f, 0, z, z, LOGO_W, LOGO_W, (const lgfx::swap565_t*)lg);
        spr->fillArc(cx, cy, lr, lr * 3 / 2 + 2, 0, 360, C_BG);
    } else {
        spr->fillCircle(cx, cy, lr, C_HDR);
        String ini = s ? s->name : String("?");
        int cl = ini.length() && ((uint8_t)ini[0] >= 0xC0) ? 2 : 1;
        ini = ini.substring(0, cl); ini.toUpperCase();
        spr->setFont(&F_BIG); spr->setTextColor(C_HTXT);
        spr->setTextDatum(lgfx::textdatum_t::middle_center); spr->drawString(ini, cx, cy); spr->setTextDatum(lgfx::textdatum_t::baseline_left);
    }
    spr->drawCircle(cx, cy, lr + 1, C_ACC);
    const int r0 = 50, maxLen = 44;
    for (int i = 0; i < NB; i++) {
        float v = ps.playing ? spB[i] : 0;
        if (v >= spPk[i]) spPk[i] = v; else spPk[i] = max(0.0f, spPk[i] - 0.012f);
        float t = (float)i / (NB - 1);
        uint16_t col = c565(T_LO.r + (T_HI.r - T_LO.r) * t, T_LO.g + (T_HI.g - T_LO.g) * t, T_LO.b + (T_HI.b - T_LO.b) * t);
        int len = 2 + (int)(v * maxLen), pk = r0 + 2 + (int)(spPk[i] * maxLen);
        const float gap = da * 0.32f;
        for (int side = 0; side < 2; side++) {
            float a0 = side ? -90 + i * da + gap : 270 - (i + 1) * da, a1 = side ? -90 + (i + 1) * da : 270 - i * da - gap;
            if (a0 < 0) { a0 += 360; a1 += 360; }               // 40 pasov × 4,5° -> nikoli čez 0°
            if (a1 > 360.01f) { a0 -= 360; a1 -= 360; }
            spr->fillArc(cx, cy, r0, r0 + len, a0, a1, col);
            if (pk > r0 + len + 1) spr->fillArc(cx, cy, pk, pk + 2, a0, a1, C_TXT);
        }
    }
    // vogali
    drawClockAt(70, 24, &F_HEAD, C_TXT, C_BG);
    footVolume(266, 24, C_SUB);
    drawWifi(8, 196);
    uint16_t fc; String st = statusText(ps, toast, fc, false);
    if (toast || !ps.playing || ps.bufferDs < 15) {
        spr->setFont(&F_TINY); spr->setTextColor(fc);
        spr->drawString(st, 312 - spr->textWidth(st), 196);
    }
    // spodaj
    spr->setFont(&F_HEAD); spr->setTextColor(C_ACC);
    marqCenter(mHead, s ? s->name : String("Ni postaj"), 160, 220, 300, 204, 224);
    String l1, l2; splitTitle(ps, s, l1, l2);
    String one = l2.length() && l1 != l2 ? l1 + " - " + l2 : l1;
    spr->setFont(&F_SMALL); spr->setTextColor(C_SUB);
    marqCenter(mL1, one, 160, 237, 300, 226, 240);
    drawOverlays(14, 50);
}

// ---------------------------------------------------------------------------
// L: ambient – zamegljeno ozadje iz barv logotipa, "steklena" kartica
// ---------------------------------------------------------------------------
static String          ambUrl;
static const uint16_t* ambLg = nullptr;
static int             ambTheme = -1;
static void buildAmbient(const Station* s, const uint16_t* lg) {
    if (!ensureBg()) return;
    const int G = 6;
    float cr[G][G], cg[G][G], cb[G][G];
    const Theme& th = THEMES[curTheme < 0 ? 0 : curTheme];
    for (int j = 0; j < G; j++)
        for (int i = 0; i < G; i++) {
            float r = 0, g = 0, b = 0;
            if (lg) {
                int x0 = i * LOGO_W / G, x1 = (i + 1) * LOGO_W / G, y0 = j * LOGO_W / G, y1 = (j + 1) * LOGO_W / G, n = 0;
                for (int y = y0; y < y1; y++)
                    for (int x = x0; x < x1; x++) {
                        uint16_t v = sw16(lg[y * LOGO_W + x]);
                        r += (v >> 11) * 255 / 31; g += ((v >> 5) & 63) * 255 / 63; b += (v & 31) * 255 / 31; n++;
                    }
                r /= n; g /= n; b /= n;
            } else {
                float t = (i + j) / (2.0f * (G - 1));
                r = th.hdr.r + (th.acc.r - th.hdr.r) * t; g = th.hdr.g + (th.acc.g - th.hdr.g) * t; b = th.hdr.b + (th.acc.b - th.hdr.b) * t;
            }
            float l = 0.3f * r + 0.59f * g + 0.11f * b;            // več nasičenosti, nato potemni
            r = l + (r - l) * 1.5f; g = l + (g - l) * 1.5f; b = l + (b - l) * 1.5f;
            cr[j][i] = constrain(r, 0.0f, 255.0f) * 0.55f; cg[j][i] = constrain(g, 0.0f, 255.0f) * 0.55f; cb[j][i] = constrain(b, 0.0f, 255.0f) * 0.55f;
        }
    auto idx = [&](int p, int n, int& i0, float& t) {
        float f = (p + 0.5f) * G / n - 0.5f;
        f = constrain(f, 0.0f, G - 1.001f);
        i0 = (int)f; t = f - i0; t = t * t * (3 - 2 * t);
    };
    uint16_t* buf = (uint16_t*)bgSpr->getBuffer();
    for (int y = 0; y < H; y++) {
        int j0; float ty; idx(y, H, j0, ty);
        for (int x = 0; x < W; x++) {
            int i0; float tx; idx(x, W, i0, tx);
            auto bil = [&](float (&c)[G][G]) {
                float a = c[j0][i0] + (c[j0][i0 + 1] - c[j0][i0]) * tx, b = c[j0 + 1][i0] + (c[j0 + 1][i0 + 1] - c[j0 + 1][i0]) * tx;
                return a + (b - a) * ty;
            };
            float dx = (x - W / 2) / (float)W, dy = (y - H / 2) / (float)H;
            float vig = 1.0f - 1.1f * (dx * dx + dy * dy);
            buf[y * W + x] = sw16(c565(bil(cr) * vig, bil(cg) * vig, bil(cb) * vig));
        }
    }
    // steklena kartica in senca sta del ozadja (riše se enkrat)
    memcpy(spr->getBuffer(), buf, W * H * 2);
    darken(18, 140, 288, 4, 170);
    tint(14, 12, 292, 128, 40, {255, 255, 255}, 12);
    memcpy(buf, spr->getBuffer(), W * H * 2);
    bgBuilt = 101; ambUrl = s ? s->url : String(); ambLg = lg; ambTheme = curTheme;
}
static void layoutAmbient(const PlayerStatus& ps, const char* toast) {
    const Station* s = curStation();
    const uint16_t* lg = s ? logoGet(s->url) : nullptr;
    String url = s ? s->url : String();
    if (!bgSpr || bgBuilt != 101 || url != ambUrl || lg != ambLg || (!lg && ambTheme != curTheme)) buildAmbient(s, lg);
    if (bgSpr) memcpy(spr->getBuffer(), bgSpr->getBuffer(), W * H * 2); else spr->fillScreen(C_BG);
    const uint16_t white = c565(255, 255, 255), soft = c565(222, 222, 232);
    drawLogo(s, 26, 28, 96);
    const int tx = 134, tw = 162;
    spr->setFont(&F_FOOT); spr->setTextColor(soft);
    char hd[64]; snprintf(hd, sizeof(hd), "%02d · %s", ui.cur + 1, s ? s->name.c_str() : "Ni postaj");
    mHead.set(hd); mHead.draw(tx, 40, tw, 26, 44);
    String l1, l2; splitTitle(ps, s, l1, l2);
    spr->setFont(&F_TITLE); spr->setTextColor(white);
    mL1.set(l1); mL1.draw(tx, 74, tw, 54, 80);
    spr->setFont(&F_SUB); spr->setTextColor(soft);
    mL2.set(l2); mL2.draw(tx, 98, tw, 84, 104);
    uint16_t fc; String st = statusText(ps, toast, fc, false);
    spr->setFont(&F_TINY); spr->setTextColor(fc == C_SUB ? soft : fc);
    spr->setClipRect(tx, 110, tw, 14); spr->drawString(st, tx, 121); spr->clearClipRect();
    // beli spekter
    const int nb = 32, step = 9, x0 = (W - nb * step) / 2 + 1, base = 206;
    spectrumCompute(spB, nb, ps.sampleRate);
    for (int i = 0; i < nb; i++) {
        int h = ps.playing ? (int)(spB[i] * 52) : 0;
        if (h < 2) h = 2;
        spr->fillRoundRect(x0 + i * step, base - h, 6, h, 2, c565(238, 238, 245));
    }
    // glasnost in ura
    darken(14, 221, 180, 5, 140);
    int fw = ui.mute ? 0 : 180 * ui.vol / VOL_STEPS;
    if (fw > 0) spr->fillRoundRect(14, 221, max(fw, 5), 5, 2, white);
    spr->setFont(&F_FOOT); spr->setTextColor(soft);
    char v[6]; snprintf(v, sizeof(v), "%d", ui.vol); spr->drawString(ui.mute ? "-" : v, 202, 229);
    spr->setFont(&F_HEAD); spr->setTextColor(white);
    String clk = clockStr(); spr->drawString(clk, 310 - spr->textWidth(clk), 232);
    drawOverlays(-1, 0);
}

static void drawMain(const PlayerStatus& ps, const char* toast) {
    switch (cfg.layout) {
        case 5:  layoutRings(ps, toast); break;
        case 6:  layoutVinyl(ps, toast); break;
        case 7:  layoutCassette(ps, toast); break;
        case 8:  layoutSynth(ps, toast); break;
        case 9:  layoutNixie(ps, toast); break;
        case 10: layoutRadial(ps, toast); break;
        case 11: layoutAmbient(ps, toast); break;
        case 1:  layoutCard(ps, toast); break;
        case 2:  layoutDial(ps, toast); break;
        case 3:  layoutClock(ps, toast); break;
        case 4:  layoutFull(ps, toast); break;
        default: layoutClassic(ps, toast); break;
    }
}

static void drawMenu() {
    spr->fillScreen(C_BG);
    spr->fillRect(0, 0, W, 34, C_HDR);
    spr->setFont(&F_HEAD); spr->setTextColor(C_HTXT);
    spr->drawString("Meni", 10, 24);
    spr->setFont(&F_SMALL);
    const char* hint = "vrti = izbira, klik = spremeni";
    spr->drawString(hint, 310 - spr->textWidth(hint), 22);
    const int rh = 29, y0 = 38;
    int n = appMenuCount();
    spr->setFont(&F_SUB);
    int first = constrain(ui.menuSel - 3, 0, max(0, n - 7));
    for (int r = 0; r < 7 && first + r < n; r++) {
        int i = first + r, y = y0 + r * rh;
        bool sel = i == ui.menuSel;
        if (sel) spr->fillRoundRect(4, y, W - 8, rh - 3, 6, C_ACC);
        String lab, val; appMenuItem(i, lab, val);
        spr->setTextColor(sel ? C_ONACC : C_TXT); spr->drawString(lab, 14, y + 20);
        if (val.length()) { spr->setTextColor(sel ? C_ONACC : C_ACC); spr->drawString(val, 304 - spr->textWidth(val), y + 20); }
    }
}

static void drawVolume() {
    spr->fillScreen(C_BG);
    spr->setFont(&F_TITLE); spr->setTextColor(C_SUB);
    center(ui.mute ? "Utišano" : "Glasnost", 50);
    spr->setFont(&F_VOL); spr->setTextColor(ui.mute ? C_DIM : C_TXT);
    char v[4]; snprintf(v, sizeof(v), "%d", ui.vol);
    center(v, 130);
    int bw = 260, bx = 30, by = 160;
    spr->fillRoundRect(bx, by, bw, 18, 9, C_LINE);
    int fw = ui.mute ? 0 : bw * ui.vol / VOL_STEPS;
    if (fw > 0) spr->fillRoundRect(bx, by, max(fw, 18), 18, 9, C_ACC);
}

static void drawBrowse() {
    bool fav = ui.favOnly, anyFav = false;
    for (auto& s : stations) if (s.fav) { anyFav = true; break; }
    std::vector<int> idx;
    for (int i = 0; i < (int)stations.size(); i++) if (!(fav && anyFav) || stations[i].fav) idx.push_back(i);
    int sel = 0;
    for (int i = 0; i < (int)idx.size(); i++) if (idx[i] == ui.browse) sel = i;

    spr->fillScreen(C_BG);
    spr->fillRect(0, 0, W, 34, C_HDR);
    spr->setFont(&F_HEAD); spr->setTextColor(C_HTXT);
    spr->drawString(fav && anyFav ? "Priljubljene" : "Postaje", 10, 24);
    char cnt[12]; snprintf(cnt, sizeof(cnt), "%d / %d", sel + 1, (int)idx.size());
    spr->setTextColor(C_SUB); spr->drawString(cnt, 310 - spr->textWidth(cnt), 24);

    const int rows = 7, rh = 29, y0 = 38;
    int first = constrain(sel - 3, 0, max(0, (int)idx.size() - rows));
    spr->setFont(&F_SUB);
    for (int r = 0; r < rows && first + r < (int)idx.size(); r++) {
        int i = idx[first + r], y = y0 + r * rh;
        bool isSel = (first + r) == sel;
        if (isSel) spr->fillRoundRect(4, y, W - 8, rh - 3, 6, C_ACC);
        uint16_t tc = isSel ? C_ONACC : C_TXT;
        if (i == ui.cur) spr->fillTriangle(10, y + 7, 10, y + 19, 17, y + 13, isSel ? C_BG : C_ACC);
        char n[5]; snprintf(n, sizeof(n), "%02d", i + 1);
        spr->setTextColor(isSel ? C_ONACC : C_DIM); spr->drawString(n, 20, y + 20);
        // ikona: logotip ali okvirček z začetnico
        const uint16_t* ic = logoIcon(stations[i].url);
        if (ic) spr->pushImage(48, y + 1, ICON_W, ICON_W, (const lgfx::swap565_t*)ic);
        else {
            spr->fillRoundRect(48, y + 1, ICON_W, ICON_W, 4, isSel ? C_HDR : C_LINE);
            const String& nm = stations[i].name; int cl = nm.length() && ((uint8_t)nm[0] >= 0xC0) ? 2 : 1;
            String ini = nm.substring(0, cl); ini.toUpperCase();
            spr->setFont(&F_FOOT); spr->setTextColor(C_TXT);
            spr->drawString(ini, 48 + (ICON_W - spr->textWidth(ini)) / 2, y + 18);
            spr->setFont(&F_SUB);
        }
        spr->setTextColor(tc);
        if (isSel) { mSel.set(stations[i].name); mSel.draw(80, y + 20, 212, y, y + rh - 3); }
        else { spr->setClipRect(80, y, 212, rh); spr->drawString(stations[i].name, 80, y + 20); spr->clearClipRect(); }
        if (stations[i].fav) { spr->setTextColor(isSel ? C_ONACC : C_YEL); spr->drawString("*", 300, y + 22); }
    }
}

static void drawStandby() {
    spr->fillScreen(C_BG);                               // barve teme (tudi svetle teme ostanejo berljive)
    if (!cfg.standby_clock) return;
    spr->setFont(&F_CLOCK); spr->setTextColor(C_TXT);
    String c = clockStr();
    drawClock((W - spr->textWidth(c)) / 2, 140, c, C_BG);
    spr->setFont(&F_SUB); spr->setTextColor(C_SUB);
    center(dateStr(), 185);
}

static void drawSaver() {
    spr->fillScreen(C_BG);
    uint32_t slot = millis() / 20000;
    int dx = (slot * 37) % 120 - 60, dy = (slot * 23) % 80 - 40;
    spr->setFont(&F_VOL); spr->setTextColor(C_SUB);
    String c = clockStr();
    spr->drawString(c, (W - spr->textWidth(c)) / 2 + dx, 130 + dy);
    const Station* s = (ui.cur >= 0 && ui.cur < (int)stations.size()) ? &stations[ui.cur] : nullptr;
    if (s) { spr->setFont(&F_SMALL); spr->setTextColor(C_DIM); spr->drawString(s->name, max<int>(0, (W - (int)spr->textWidth(s->name)) / 2 + dx), 160 + dy); }
}

static void drawInfo() {
    spr->fillScreen(C_BG);
    spr->fillRect(0, 0, W, 38, C_HDR);
    spr->setFont(&F_HEAD); spr->setTextColor(C_HTXT);
    center(ui.screen == SCR_SAFE ? "VARNI NAČIN" : ui.screen == SCR_AP ? "Nastavitev WiFi" : "ZRadio", 26);
    spr->setFont(&F_SUB);
    center(ui.info1, 100); spr->setTextColor(C_SUB);
    center(ui.info2, 135); center(ui.info3, 170);
}

// ---------------------------------------------------------------------------
// Pošiljanje: le pasovi po 16 vrstic, ki so se spremenili
// ---------------------------------------------------------------------------
static void push(bool full) {
    const uint32_t* p = (const uint32_t*)spr->getBuffer();
    const int words = W * BAND / 2;
    bool any = false;
    // spremenjene pasove po 8 vrstic, sosednje združene v en kos -> manj ločenih posodobitev (manj "migetanja")
    bool chg[NBANDS];
    for (int b = 0; b < NBANDS; b++) {
        const uint32_t* q = p + b * words;
        uint32_t h = 2166136261u;
        for (int i = 0; i < words; i++) h = (h ^ q[i]) * 16777619u;
        chg[b] = full || !sumsValid || h != bandSum[b];
        bandSum[b] = h;
    }
    tft->startWrite();
    for (int b = 0; b < NBANDS; ) {
        if (!chg[b]) { b++; continue; }
        int e = b;
        while (e + 1 < NBANDS && (chg[e + 1] || (e + 2 < NBANDS && chg[e + 2]))) e++;   // premostimo tudi en nespremenjen pas
        if (e + 1 < NBANDS && chg[e + 1]) e++;
        tft->pushImage(0, b * BAND, W, (e - b + 1) * BAND, (const lgfx::swap565_t*)spr->getBuffer() + b * BAND * W);
        any = true;
        b = e + 1;
    }
    tft->endWrite();
    sumsValid = true;
    if (any) lastPush = millis();
}

bool tftBegin() {
    initColors();
    tft = new LGFX_ILI9341();
    if (!tft->init()) { diagEvent("TFT: inicializacija ni uspela"); delete tft; tft = nullptr; return false; }
    tft->setRotation(cfg.rot180 ? 3 : 1);
    tft->fillScreen(TFT_BLACK);
    spr = new LGFX_Sprite(tft);
    spr->setColorDepth(16);
    spr->setPsram(true);
    if (!spr->createSprite(W, H)) { diagEvent("TFT: ni pomnilnika za sliko"); delete spr; spr = nullptr; return false; }
    spr->setTextDatum(lgfx::textdatum_t::baseline_left);
    spr->setTextWrap(false);
    tftApplyConfig();
    diagEvent("TFT: ILI9341 320x240 pripravljen");
    return true;
}

bool tftActive() { return tft && spr; }

void tftApplyConfig() {
    if (!tft) return;
    tft->setRotation(cfg.rot180 ? 3 : 1);
    sumsValid = false;
    curBright = 0;          // ponovno nastavi ob izrisu
}

void tftUpdate(bool force, const char* toast) {
    if (!tftActive()) return;
    uint32_t now = millis();

    if (cfg.refresh_min && now - lastRefresh > (uint32_t)cfg.refresh_min * 60000UL) {
        lastRefresh = now;
        diagCount(DC_OLED_REINIT);
        tft->init(); tft->setRotation(cfg.rot180 ? 3 : 1);
        sumsValid = false; curBright = 0; force = true;
    }

    // tema: ponoči po želji nočna tema
    int th = (ui.night && cfg.night_theme >= 0) ? cfg.night_theme : cfg.theme;
    if (th != curTheme) { applyTheme(th); force = true; }

    uint32_t interval = 40;                       // ~25 sl/s (VU, drseče besedilo)
    if (ui.screen == SCR_MAIN && cfg.layout >= 4 && cfg.layout != 9) interval = 50;   // celozaslonski: več pasov za pošiljanje
    if (ui.screen == SCR_STANDBY) interval = 250;
    else if (ui.screen == SCR_SAVER) interval = 1000;
    else if (ui.screen != SCR_MAIN && ui.screen != SCR_BROWSE && ui.screen != SCR_VOLUME && ui.screen != SCR_MENU) interval = 250;
    if (!force && now - lastDraw < interval) return;
    lastDraw = now;

    // svetlost osvetlitve: "kontrast" iz nastavitev = svetlost (0..255)
    bool off = ui.screen == SCR_STANDBY && !cfg.standby_clock;
    uint8_t br = off ? 0 : (ui.night || ui.screen == SCR_STANDBY || ui.screen == SCR_SAVER) ? cfg.contrast_night : cfg.contrast;
    if (!off && br < 2) br = 2;
    if (br != curBright) { tft->setBrightness(br); curBright = br; }
    if (off) {
        if (!dark) { spr->fillScreen(TFT_BLACK); push(true); dark = true; }
        return;
    }
    if (dark) { dark = false; force = true; }

    spectrumEnable((cfg.viz == "spectrum" || cfg.viz == "scope" || cfg.viz == "mirror" || cfg.layout == 5 || cfg.layout == 6 || cfg.layout == 8 || cfg.layout >= 10) && ui.screen == SCR_MAIN);
    PlayerStatus ps = playerStatus();
    switch (ui.screen) {
        case SCR_MAIN:    drawMain(ps, toast); break;
        case SCR_VOLUME:  drawVolume(); break;
        case SCR_BROWSE:  drawBrowse(); break;
        case SCR_MENU:    drawMenu(); break;
        case SCR_STANDBY: drawStandby(); break;
        case SCR_SAVER:   drawSaver(); break;
        default:          drawInfo(); break;
    }
    appUnlock();               // pošiljanje po SPI ne potrebuje stanja -> splet medtem ni blokiran
    push(force);
    appLock();
}

size_t tftSnapshot(uint8_t* out, size_t max, uint32_t& ageMs) {
    if (!tftActive() || max < 160 * 120) return 0;
    const uint16_t* b = (const uint16_t*)spr->getBuffer();
    for (int y = 0; y < 120; y++)
        for (int x = 0; x < 160; x++) {
            uint16_t v = b[(y * 2) * W + x * 2];
            v = (v >> 8) | (v << 8);                  // sprite hrani barve z zamenjanimi bajti
            uint8_t r = v >> 11, g = (v >> 5) & 63, bl = v & 31;
            out[y * 160 + x] = ((r >> 2) << 5) | ((g >> 3) << 2) | (bl >> 3);
        }
    ageMs = millis() - lastPush;
    return 160 * 120;
}
