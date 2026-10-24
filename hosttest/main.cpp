#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include "app.h"
#include "player.h"
#include "stations.h"
#include "logo.h"
#include "diag.h"
#include <map>
#include <WiFi.h>
#include <LittleFS.h>
static uint32_t fakeMs = 100500;
uint32_t millis() { return fakeMs; }
WiFiC WiFi; LFS LittleFS;
UiModel ui; Config cfg; Config::Config() {} bool safeMode = false;
std::vector<Station> stations;
bool appTimeValid() { return true; }
int appMenuCount() { return 0; } void appMenuItem(int, String&, String&) {}
uint32_t playerTitleSeq() { return 1; }
static PlayerStatus PS;
PlayerStatus playerStatus() { return PS; }
void diagCount(DiagCounter) {}
static uint16_t logoBuf[64 * 64]; static bool haveLogo = true;
const uint16_t* logoGet(const String&) { return haveLogo ? logoBuf : nullptr; }
const uint16_t* logoIcon(const String&) { return nullptr; }
void spectrumEnable(bool) {}
bool spectrumCompute(float* b, int n, uint32_t) { for (int i = 0; i < n; i++) { float t = (float)i / n; b[i] = std::max(0.05f, 0.85f - 0.6f * t + 0.18f * sinf(i * 1.7f + fakeMs * 0.001f)); } return true; }
bool spectrumScope(int16_t* o, int n, int) { for (int i = 0; i < n; i++) o[i] = 8000 * sinf(i * 0.1f); return true; }
LGFX_Sprite* hostSprite();
void hostDraw(int layout, int theme, const char* toast);
int benchMain(int, char**);
int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "bench") { stations = {{"Radio Veseljak", "http://a", "narodna", true}, {"Val 202", "http://b", "pop"}}; PS.playing = true; PS.sampleRate = 44100; PS.title = "Avsenik - Na Golici"; PS.vu = (90<<8)|75; PS.bufferDs = 40; PS.url = "http://a"; for (int y = 0; y < 64; y++) for (int x = 0; x < 64; x++) logoBuf[y*64+x] = 0x1234; return benchMain(argc, argv); }
    // logo: modro-rdeč krog na belem s črko
    for (int y = 0; y < 64; y++) for (int x = 0; x < 64; x++) {
        int dx = x - 32, dy = y - 32; uint16_t c = lgfx::color565(250, 250, 250);
        if (dx * dx + dy * dy < 26 * 26) c = x < 32 ? lgfx::color565(220, 40, 60) : lgfx::color565(30, 90, 200);
        if (abs(dx) < 4 && abs(dy) < 18) c = lgfx::color565(255, 255, 255);
        logoBuf[y * 64 + x] = (c >> 8) | (c << 8);
    }
    stations = {{"Radio Veseljak", "http://a", "narodna", true}, {"Val 202", "http://b", "pop"}, {"Radio Center", "http://c", "pop"}, {"Radio Ognjišče", "http://d", ""}, {"Radio 1", "http://e", ""}};
    ui.cur = 0; ui.vol = 14;
    PS.playing = true; PS.wanted = true; PS.url = "http://a"; PS.title = "Avsenik - Na Golici"; PS.codec = "mp3"; PS.bitrate = 128000; PS.sampleRate = 44100; PS.bufferPct = 80; PS.bufferDs = 40; PS.vu = (90 << 8) | 75;
    if (argc > 4) cfg.viz = argv[4];
    int layout = atoi(argv[1]), theme = argc > 2 ? atoi(argv[2]) : 0; haveLogo = argc > 3 ? atoi(argv[3]) : 1;
    for (int f = 0; f < 30; f++) { fakeMs += 50; hostDraw(layout, theme, nullptr); }
    LGFX_Sprite* s = hostSprite();
    char fn[64]; snprintf(fn, sizeof(fn), "out_%d_%d_%d.ppm", layout, theme, haveLogo);
    FILE* f = fopen(fn, "wb"); fprintf(f, "P6 320 240 255\n");
    const uint16_t* b = (const uint16_t*)s->getBuffer();
    for (int i = 0; i < 320 * 240; i++) { uint16_t v = (b[i] >> 8) | (b[i] << 8); uint8_t px[3] = {(uint8_t)((v >> 11) * 255 / 31), (uint8_t)(((v >> 5) & 63) * 255 / 63), (uint8_t)((v & 31) * 255 / 31)}; fwrite(px, 1, 3, f); }
    fclose(f);
}
