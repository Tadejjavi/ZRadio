#include "spectrum.h"
#include <math.h>

static constexpr int N = 1024, LOGN = 10, RING = 2048;
static int16_t           ring[RING];
static volatile uint32_t wpos = 0;
static volatile bool     enabled = false;
static volatile uint32_t lastTap = 0;

// Kliče knjižnica iz dekodirnega opravila: samo kopija (mono) v krožni medpomnilnik
void zr_pcm_tap(const int16_t* lr, int32_t frames) {
    if (!enabled) return;
    uint32_t w = wpos;
    for (int32_t i = 0; i < frames; i++) ring[(w + i) & (RING - 1)] = (int16_t)(((int32_t)lr[2 * i] + lr[2 * i + 1]) >> 1);
    wpos = w + frames;
    lastTap = millis();
}

void spectrumEnable(bool on) { enabled = on; }

static float *re, *im, *win, *cosT, *sinT;            // v PSRAM (16 kB manj notranjega RAM-a)
static bool  tablesReady = false;
static float smooth[64];

static void initTables() {
    re = (float*)ps_malloc(N * 4); im = (float*)ps_malloc(N * 4); win = (float*)ps_malloc(N * 4);
    cosT = (float*)ps_malloc(N * 2); sinT = (float*)ps_malloc(N * 2);
    for (int i = 0; i < N; i++) win[i] = 0.5f - 0.5f * cosf(2 * PI * i / (N - 1));
    for (int i = 0; i < N / 2; i++) { cosT[i] = cosf(2 * PI * i / N); sinT[i] = -sinf(2 * PI * i / N); }
    tablesReady = true;
}

static void fft() {
    for (int i = 1, j = 0; i < N; i++) {                 // obrat bitov
        int bit = N >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { float t = re[i]; re[i] = re[j]; re[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; }
    }
    for (int len = 2; len <= N; len <<= 1) {
        int half = len >> 1, step = N / len;
        for (int i = 0; i < N; i += len)
            for (int k = 0; k < half; k++) {
                float wr = cosT[k * step], wi = sinT[k * step];
                int a = i + k, b = a + half;
                float xr = re[b] * wr - im[b] * wi, xi = re[b] * wi + im[b] * wr;
                re[b] = re[a] - xr; im[b] = im[a] - xi;
                re[a] += xr;        im[a] += xi;
            }
    }
}

bool spectrumCompute(float* bands, int n, uint32_t rate) {
    if (n > 64) n = 64;
    if (!tablesReady) initTables();
    bool fresh = enabled && millis() - lastTap < 300;
    if (fresh) {
        uint32_t w = wpos;
        for (int i = 0; i < N; i++) { re[i] = ring[(w - N + i) & (RING - 1)] * win[i]; im[i] = 0; }
        fft();
    }
    if (!rate) rate = 44100;
    const float fMin = 40.0f, fMax = min(16000.0f, rate / 2.0f - 100);
    const float norm = 32767.0f * N / 4;                 // polna skala sinusa z Hannovim oknom
    for (int b = 0; b < n; b++) {
        float target = 0;
        if (fresh) {
            float f0 = fMin * powf(fMax / fMin, (float)b / n), f1 = fMin * powf(fMax / fMin, (float)(b + 1) / n);
            int k0 = max(1, (int)(f0 * N / rate)), k1 = max(k0, (int)(f1 * N / rate));
            float pk = 0;
            for (int k = k0; k <= k1 && k < N / 2; k++) { float m = re[k] * re[k] + im[k] * im[k]; if (m > pk) pk = m; }
            float db = 10.0f * log10f(pk / (norm * norm) + 1e-12f);   // 0 dB = polna skala
            db += 3.0f * b / n * 4.0f;                                  // nagib: visoki toni so v glasbi šibkejši
            target = constrain((db + 63.0f) / 62.0f, 0.0f, 1.0f);       // -63 .. -1 dB -> 0 .. 1
        }
        smooth[b] = target > smooth[b] ? target : smooth[b] * 0.82f + target * 0.18f;
        bands[b] = smooth[b];
    }
    return fresh;
}

bool spectrumScope(int16_t* out, int n, int decim) {
    if (!enabled || millis() - lastTap > 300) return false;
    uint32_t w = wpos;
    int need = n * decim, search = 600;
    if (need + search > RING - 64) search = RING - 64 - need;
    uint32_t start = w - need - search;                 // poišči prehod skozi nič navzgor v starejšem delu
    int trig = 0;
    for (int i = 1; i < search; i++) {
        int16_t a = ring[(start + i - 1) & (RING - 1)], b = ring[(start + i) & (RING - 1)];
        if (a < 0 && b >= 0) { trig = i; break; }
    }
    for (int i = 0; i < n; i++) out[i] = ring[(start + trig + i * decim) & (RING - 1)];
    return true;
}
