#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <LittleFS.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <deque>
#include <map>
#include "logo.h"
#include "diag.h"
#include "flashguard.h"
#include <esp_heap_caps.h>
// Opomba: TLS ostane v notranjem RAM-u. Preusmeritev mbedTLS v PSRAM (0.5.3) je zrušila WiFi
// (WiFi gonilnik uporablja isti alokator mbedTLS) – zato le preverjanje prostega RAM-a pred prenosom.

// ---------------------------------------------------------------------------
// Datoteke
// ---------------------------------------------------------------------------
static uint32_t fnv(const String& s) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < s.length(); i++) h = (h ^ (uint8_t)s[i]) * 16777619u;
    return h;
}
String logoPath(const String& u) { char b[24]; snprintf(b, sizeof(b), "/logo/%08lx.l2", (unsigned long)fnv(u)); return b; }

static volatile uint32_t logoGen = 1;          // poveča se ob vsaki spremembi -> zaslon ponovno naloži

bool logoSave(const String& u, const uint8_t* data, size_t len) {
    if (len != LOGO_BYTES) return false;
    FlashGuard g("logotip");
    if (!LittleFS.exists("/logo")) LittleFS.mkdir("/logo");
    String p = logoPath(u), tmp = p + ".tmp";
    File f = LittleFS.open(tmp, "w");
    if (!f) return false;
    size_t n = f.write(data, len); f.close();
    if (n != len) { LittleFS.remove(tmp); return false; }
    LittleFS.remove(p);
    bool ok = LittleFS.rename(tmp, p);
    logoGen = logoGen + 1;
    return ok;
}
bool logoDelete(const String& u) {
    FlashGuard g("logotip");
    bool ok = LittleFS.remove(logoPath(u));
    logoGen = logoGen + 1;
    return ok;
}
bool logoExists(const String& u) { return LittleFS.exists(logoPath(u)); }
void logoInvalidate(const String&) { logoGen = logoGen + 1; }

// ---------------------------------------------------------------------------
// Predpomnilnik za zaslon (uporablja ga samo opravilo zaslona -> brez zaklepanja)
// ---------------------------------------------------------------------------
struct Entry { uint16_t* big = nullptr; uint16_t* icon = nullptr; uint32_t gen = 0; };
static std::map<uint32_t, Entry> cache;

static inline void unpack(uint16_t v, int& r, int& g, int& b) {      // shranjeno z zamenjanimi bajti
    v = (v >> 8) | (v << 8);
    r = (v >> 11) & 31; g = (v >> 5) & 63; b = v & 31;
}
static inline uint16_t pack(int r, int g, int b) { uint16_t v = (r << 11) | (g << 5) | b; return (v >> 8) | (v << 8); }

static void makeIcon(const uint16_t* src, uint16_t* dst) {           // 64 -> 24, povprečje površine
    const float k = (float)LOGO_W / ICON_W;
    for (int y = 0; y < ICON_W; y++)
        for (int x = 0; x < ICON_W; x++) {
            int x0 = (int)(x * k), x1 = (int)((x + 1) * k), y0 = (int)(y * k), y1 = (int)((y + 1) * k);
            int sr = 0, sg = 0, sb = 0, n = 0;
            for (int yy = y0; yy < y1; yy++) for (int xx = x0; xx < x1; xx++) {
                int r, g, b; unpack(src[yy * LOGO_W + xx], r, g, b); sr += r; sg += g; sb += b; n++;
            }
            dst[y * ICON_W + x] = n ? pack(sr / n, sg / n, sb / n) : 0;
        }
}

static Entry* load(const String& u) {
    uint32_t h = fnv(u);
    Entry& e = cache[h];
    if (e.gen == logoGen) return &e;
    if (e.big) { free(e.big); e.big = nullptr; }
    if (e.icon) { free(e.icon); e.icon = nullptr; }
    e.gen = logoGen;
    File f = LittleFS.open(logoPath(u), "r");
    if (!f) return &e;
    if (f.size() == LOGO_BYTES) {
        e.big = (uint16_t*)ps_malloc(LOGO_BYTES);
        e.icon = (uint16_t*)ps_malloc(ICON_BYTES);
        if (e.big && e.icon && f.read((uint8_t*)e.big, LOGO_BYTES) == LOGO_BYTES) makeIcon(e.big, e.icon);
        else { free(e.big); free(e.icon); e.big = e.icon = nullptr; }
    }
    f.close();
    return &e;
}
const uint16_t* logoGet(const String& u)  { return u.length() ? load(u)->big : nullptr; }
const uint16_t* logoIcon(const String& u) { return u.length() ? load(u)->icon : nullptr; }

// ---------------------------------------------------------------------------
// Prenos in pretvorba
// ---------------------------------------------------------------------------
struct Job { String station, img; uint32_t rawId = 0; };   // rawId != 0: le prenos (posrednik za brskalnik)
static uint8_t*  rawBuf = nullptr;
static size_t    rawLen = 0;
static volatile uint32_t rawDone = 0, rawNext = 0;
static std::deque<Job> jobs;
static SemaphoreHandle_t jmtx = nullptr;
static volatile int busy = 0;
static String lastMsg;

static void setMsg(const String& m) { xSemaphoreTake(jmtx, portMAX_DELAY); lastMsg = m; xSemaphoreGive(jmtx); diagEvent("Logotip: %s", m.c_str()); }

bool logoFetch(const String& station, const String& img) {
    if (!jmtx || !img.startsWith("http")) return false;
    xSemaphoreTake(jmtx, portMAX_DELAY);
    bool ok = jobs.size() < 64;
    if (ok) jobs.push_back({station, img});
    xSemaphoreGive(jmtx);
    return ok;
}
uint32_t logoGetRaw(const String& img) {
    if (!jmtx || !img.startsWith("http")) return 0;
    xSemaphoreTake(jmtx, portMAX_DELAY);
    uint32_t id = 0;
    if (jobs.size() < 64) { id = ++rawNext; Job j; j.img = img; j.rawId = id; jobs.push_back(j); }
    xSemaphoreGive(jmtx);
    return id;
}
uint32_t logoRawDone() { return rawDone; }
const uint8_t* logoRaw(size_t& len) { len = rawLen; return rawBuf; }

void logoStatus(int& pending, String& last) {
    if (!jmtx) { pending = 0; last = ""; return; }
    xSemaphoreTake(jmtx, portMAX_DELAY); pending = jobs.size() + busy; last = lastMsg; xSemaphoreGive(jmtx);
}

// zapis odgovora v PSRAM (writeToStream pravilno obdela tudi "chunked" prenos)
class PsBuf : public Stream {
public:
    uint8_t* buf = nullptr; size_t len = 0, cap = 0; bool over = false;
    explicit PsBuf(size_t c) : cap(c) { buf = (uint8_t*)ps_malloc(c); }
    size_t write(uint8_t b) override { return write(&b, 1); }
    size_t write(const uint8_t* d, size_t n) override {
        if (!buf || len + n > cap) { over = true; return 0; }
        memcpy(buf + len, d, n); len += n; return n;
    }
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
    void flush() override {}
};

static uint8_t* download(String url, size_t& len, String& err) {
    const size_t MAXB = 400 * 1024;
    HTTPClient http;
    WiFiClient plain; WiFiClientSecure secure;
    secure.setInsecure();
    static const char* hdrs[] = {"Location"};
    int code = 0;
    for (int hop = 0; hop < 4; hop++) {                 // preusmeritve ročno: http -> https potrebuje drug odjemalec
        bool ok = url.startsWith("https") ? http.begin(secure, url) : http.begin(plain, url);
        if (!ok) { err = "neveljaven URL"; return nullptr; }
        http.setTimeout(8000);
        http.setUserAgent("ZRadio");
        http.collectHeaders(hdrs, 1);
        code = http.GET();
        if (code == 301 || code == 302 || code == 303 || code == 307 || code == 308) {
            String loc = http.header("Location");
            http.end();
            if (loc.isEmpty()) break;
            if (loc.startsWith("/")) { int p = url.indexOf('/', url.indexOf("//") + 2); loc = (p > 0 ? url.substring(0, p) : url) + loc; }
            url = loc;
            continue;
        }
        break;
    }
    if (code != 200) { err = "HTTP " + String(code); http.end(); return nullptr; }
    int clen = http.getSize();
    if (clen > (int)MAXB) { err = "slika je prevelika"; http.end(); return nullptr; }
    PsBuf out(clen > 0 ? clen : MAXB);
    if (!out.buf) { err = "ni pomnilnika"; http.end(); return nullptr; }
    int n = http.writeToStream(&out);
    http.end();
    if (n < 16 || out.over) { free(out.buf); err = out.over ? "slika je prevelika" : "prazen odgovor"; return nullptr; }
    len = out.len;
    return out.buf;
}

static bool pngSize(const uint8_t* d, size_t len, int& w, int& h) {
    if (len < 24 || memcmp(d, "\x89PNG", 4)) return false;
    w = (d[16] << 24) | (d[17] << 16) | (d[18] << 8) | d[19];
    h = (d[20] << 24) | (d[21] << 16) | (d[22] << 8) | d[23];
    return w > 0 && h > 0;
}
static bool jpgSize(const uint8_t* d, size_t len, int& w, int& h, bool& progressive) {
    if (len < 4 || d[0] != 0xFF || d[1] != 0xD8) return false;
    size_t i = 2;
    while (i + 9 < len) {
        if (d[i] != 0xFF) { i++; continue; }
        uint8_t m = d[i + 1];
        if (m == 0xC0 || m == 0xC1 || m == 0xC2) {
            progressive = m == 0xC2;
            h = (d[i + 5] << 8) | d[i + 6]; w = (d[i + 7] << 8) | d[i + 8];
            return w > 0 && h > 0;
        }
        size_t seg = (d[i + 2] << 8) | d[i + 3];
        i += 2 + seg;
    }
    return false;
}
// ICO: poišči največjo sliko, ki je shranjena kot PNG
static bool icoPng(const uint8_t* d, size_t len, const uint8_t*& png, size_t& plen) {
    if (len < 6 || d[0] || d[1] || d[2] != 1 || d[3]) return false;
    int count = d[4] | (d[5] << 8), best = -1;
    for (int k = 0; k < count && 6 + k * 16 + 16 <= (int)len; k++) {
        const uint8_t* e = d + 6 + k * 16;
        int w = e[0] ? e[0] : 256;
        uint32_t sz = e[8] | (e[9] << 8) | (e[10] << 16) | (e[11] << 24);
        uint32_t off = e[12] | (e[13] << 8) | (e[14] << 16) | (e[15] << 24);
        if (off >= len || sz > len - off || sz < 8 || memcmp(d + off, "\x89PNG", 4)) continue;
        if (w > best) { best = w; png = d + off; plen = sz; }
    }
    return best > 0;
}

static void processRaw(const Job& j) {
    String err; size_t len = 0;
    uint8_t* data = download(j.img, len, err);
    if (rawBuf) { free(rawBuf); rawBuf = nullptr; rawLen = 0; }
    if (data) { rawBuf = data; rawLen = len; }
    rawDone = j.rawId;
    setMsg(data ? "prenesen za pretvorbo v brskalniku (" + String(len) + " B)" : "prenos ni uspel (" + err + "): " + j.img);
}

static void process(const Job& j) {
    if (j.rawId) { processRaw(j); return; }
    String err;
    size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (largest < 24 * 1024) { setMsg("premalo notranjega RAM-a (" + String(largest / 1024) + " kB), preskočeno"); return; }
    size_t len = 0;
    uint8_t* data = download(j.img, len, err);
    if (!data) { setMsg("prenos ni uspel (" + err + "): " + j.img); return; }
    const uint8_t* img = data; size_t ilen = len;
    int w = 0, h = 0; bool prog = false, isPng = false;
    if (icoPng(data, len, img, ilen)) isPng = pngSize(img, ilen, w, h);
    else if (pngSize(data, len, w, h)) isPng = true;
    else if (!jpgSize(data, len, w, h, prog)) {
        bool svg = data[0] == '<';
        free(data);
        setMsg(svg ? "SVG ni podprt" : "oblika ni podprta (le PNG, JPEG, ICO s PNG)");
        return;
    }
    if (prog) { free(data); setMsg("progresivni JPEG ni podprt"); return; }

    LGFX_Sprite s;
    s.setColorDepth(16); s.setPsram(true);
    if (!s.createSprite(LOGO_W, LOGO_W)) { free(data); setMsg("ni pomnilnika za sliko"); return; }
    s.fillScreen(TFT_WHITE);                                   // prozorni deli -> bela podlaga
    float sc = min((float)LOGO_W / w, (float)LOGO_W / h);
    if (sc > 4) sc = 4;
    // sredinjenje izračunamo sami (datum brez okvirja maxWidth/maxHeight ne centrira)
    int dx = (int)((LOGO_W - w * sc) / 2), dy = (int)((LOGO_W - h * sc) / 2);
    bool ok = isPng ? s.drawPng(img, ilen, dx, dy, 0, 0, 0, 0, sc, sc)
                    : s.drawJpg(img, ilen, dx, dy, 0, 0, 0, 0, sc, sc);
    free(data);
    if (!ok) { s.deleteSprite(); setMsg("slike ni bilo mogoče dekodirati"); return; }
    ok = logoSave(j.station, (const uint8_t*)s.getBuffer(), LOGO_BYTES);
    s.deleteSprite();
    setMsg(ok ? String("shranjen (") + w + "x" + h + ")" : String("shranjevanje ni uspelo"));
}

static void logoTask(void*) {
    for (;;) {
        Job j; bool have = false;
        xSemaphoreTake(jmtx, portMAX_DELAY);
        if (!jobs.empty()) { j = jobs.front(); jobs.pop_front(); have = true; busy = 1; }
        xSemaphoreGive(jmtx);
        if (have) {
            if (WiFi.status() == WL_CONNECTED) process(j); else setMsg("ni WiFi");
            busy = 0;
        }
        vTaskDelay(pdMS_TO_TICKS(have ? 800 : 300));   // med prenosi kratek premor (WiFi, predvajanje)
    }
}

void logoBegin() {
    jmtx = xSemaphoreCreateMutex();
    if (!LittleFS.exists("/logo")) LittleFS.mkdir("/logo");
    else {                                                     // pobriši logotipe stare oblike (.565)
        std::vector<String> old;
        File d = LittleFS.open("/logo");
        for (File f = d.openNextFile(); f; f = d.openNextFile()) { String n = f.name(); if (n.endsWith(".565") || n.endsWith(".tmp")) old.push_back(n); f.close(); }
        d.close();
        if (!old.empty()) { FlashGuard g("logotipi"); for (auto& n : old) LittleFS.remove("/logo/" + n); diagEvent("Logotip: odstranjenih %u starih", (unsigned)old.size()); }
    }
    // prioriteta 0 (kot IDLE): TLS rokovanje (ECDH) zna trajati več sekund in ne sme izstradati IDLE0 (task watchdog)
    xTaskCreatePinnedToCore(logoTask, "logo", 16384, nullptr, 0, nullptr, 0);
}
