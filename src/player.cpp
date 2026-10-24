#include "player.h"
#include "config.h"
#include <Audio.h>
#include "diag.h"

enum PCmd : uint8_t { P_PLAY, P_STOP, P_VOL, P_MUTE, P_TONE, P_MONO };
struct PMsg {
    PCmd    cmd;
    int16_t a = 0, b = 0, c = 0;
    char*   url = nullptr;      // lasti prejemnik (free)
};

static constexpr int     FADE_K  = 4;          // finejša lestvica za mehke prehode
static Audio*            audio   = nullptr;
static QueueHandle_t     q       = nullptr;
static SemaphoreHandle_t mtx     = nullptr;
static PlayerStatus      st;
static volatile uint32_t titleSeq = 0;

static void lock()   { xSemaphoreTake(mtx, portMAX_DELAY); }
static void unlock() { xSemaphoreGive(mtx); }

static void send(PMsg m) {
    if (!q) { if (m.url) free(m.url); return; }
    if (xQueueSend(q, &m, pdMS_TO_TICKS(200)) != pdTRUE && m.url) free(m.url);
}

void playerPlay(const String& url) { PMsg m{P_PLAY}; m.url = strdup(url.c_str()); send(m); }
void playerStop()                  { send(PMsg{P_STOP}); }
void playerVolume(uint8_t v)       { PMsg m{P_VOL};  m.a = v; send(m); }
void playerMute(bool mu)           { PMsg m{P_MUTE}; m.a = mu; send(m); }
void playerTone(int8_t l, int8_t md, int8_t h) { PMsg m{P_TONE}; m.a = l; m.b = md; m.c = h; send(m); }
void playerMono(bool mo)           { PMsg m{P_MONO}; m.a = mo; send(m); }

PlayerStatus playerStatus() {
    if (!mtx) return PlayerStatus();
    lock(); PlayerStatus s = st; unlock();
    return s;
}
uint32_t playerTitleSeq() { return titleSeq; }
uint32_t playerDecoderStackFree() { return audio ? audio->getHighWatermark() : 0; }   // ESP-IDF: že v bajtih


static void logAudio(const char* msg) {
    char clean[140]; size_t j = 0;                // odstrani barvne ANSI kode iz sporočil knjižnice
    for (const char* p = msg; *p && j < sizeof(clean) - 1; p++) {
        if (*p == 0x1b) { while (*p && *p != 'm') p++; if (!*p) break; continue; }
        clean[j++] = *p;
    }
    clean[j] = 0;
    diagEvent("Zvok: %s", clean);
}

// Povratni klic knjižnice teče na NJENEM opravilu (statičen sklad ~14 KB, tik pred dekodiranjem).
// Zato tu ne delamo ničesar drugega kot kopijo sporočila v vrsto – vse ostalo obdela naše opravilo.
struct AMsg { uint8_t e; char msg[124]; };
static QueueHandle_t aq = nullptr;

static void onAudioInfo(Audio::msg_t m) {
    if (!m.msg || !aq) return;
    AMsg x;
    x.e = (uint8_t)m.e;
    strlcpy(x.msg, m.msg, sizeof(x.msg));
    xQueueSend(aq, &x, 0);              // ne čakaj – če je vrsta polna, sporočilo izpustimo
}

static bool     announcePending = false;   // vrstica »Tok: …« po prvem okvirju nove postaje
static uint32_t lastRate = 0;

static void handleAudioMsg(const AMsg& x) {
    const char* msg = x.msg;
    if (announcePending) {
        const char* p = strstr(msg, "SampleRate (Hz): ");
        if (p) lastRate = strtoul(p + 17, nullptr, 10);
        if (strstr(msg, "BitsPerSample")) {          // zadnje sporočilo o parametrih novega toka
            announcePending = false;
            lock(); uint32_t br = st.bitrate; unlock();
            if (!br) br = audio->getBitRate();
            if (br) diagEvent("Tok: %s, %lu Hz, %lu kb/s", audio->getCodecname(), (unsigned long)lastRate, (unsigned long)(br / 1000));
            else    diagEvent("Tok: %s, %lu Hz", audio->getCodecname(), (unsigned long)lastRate);
        }
    }
    switch (x.e) {
        case Audio::evt_streamtitle:
            lock(); st.title = msg; st.title.trim(); unlock(); titleSeq = titleSeq + 1; break;
        case Audio::evt_name:
            lock(); st.icyName = msg; unlock(); break;
        case Audio::evt_bitrate:
            lock(); st.bitrate = strtoul(msg, nullptr, 10); unlock(); break;
        case Audio::evt_eof:
            break;
        default:
            if (strstr(msg, "rror") || strstr(msg, "failed") || strstr(msg, "refused")) {
                lock(); bool fresh = st.lastError != msg; st.lastError = msg; unlock();
                if (fresh) logAudio(msg);
            }
            break;
    }
}

static void audioTask(void*) {
    String   wantUrl;
    bool     want = false;
    uint32_t nextTry = 0, backoff = 2000;
    uint32_t lastStat = 0;
    uint32_t emptySince = 0;          // čuvaj: tok "teče", a podatkov ni
    uint8_t  curVol = 0;
    bool     muted = false;           // utišanje prek glasnosti
    // mehki prehodi: knjižnica ima FADE_K-krat finejšo lestvico glasnosti
    int      level = 0;               // trenutna glasnost knjižnice (0..VOL_STEPS*FADE_K)
    uint32_t lastFade = 0, playAt = 0;
    auto setLevel = [&](int l) { level = constrain(l, 0, VOL_STEPS * FADE_K); audio->setVolume(level); };
    auto fadeOut = [&](uint32_t ms) {                // blokirajoče, a zvok med tem teče naprej
        if (level <= 0 || !audio->isRunning()) { setLevel(0); return; }
        int start = level, n = max<int>(1, ms / 10);
        for (int i = 1; i <= n; i++) { setLevel(start * (n - i) / n); audio->loop(); vTaskDelay(pdMS_TO_TICKS(10)); }
    };
    PMsg m;

    for (;;) {
        while (xQueueReceive(q, &m, 0) == pdTRUE) {
            switch (m.cmd) {
                case P_PLAY:
                    wantUrl = m.url; free(m.url);
                    diagEvent("Predvajam %s", wantUrl.c_str());
                    announcePending = true; lastRate = 0; playAt = millis();
                    want = true; backoff = 2000; nextTry = 0;
                    fadeOut(250);
                    audio->stopSong();
                    setLevel(0);                                  // nova postaja se začne tiho in se dvigne
                    lock(); st.title = ""; st.icyName = ""; st.bitrate = 0; st.codec = "";
                    st.url = wantUrl; st.wanted = true; st.lastError = ""; unlock();
                    titleSeq = titleSeq + 1;
                    break;
                case P_STOP:
                    fadeOut(300);
                    want = false; audio->stopSong();
                    lock(); st.wanted = false; st.playing = false; st.connecting = false; unlock();
                    break;
                case P_VOL:  curVol = m.a; break;            // dejansko glasnost nastavlja prehod spodaj
                case P_MUTE: muted = m.a; break;
                case P_TONE: audio->setTone(m.a, m.b, m.c); break;
                case P_MONO: audio->forceMono(m.a); break;
            }
        }

        if (want && !audio->isRunning() && millis() >= nextTry) {
            lock(); st.connecting = true; unlock();
            bool ok = audio->connecttohost(wantUrl.c_str());   // blokira do povezave/napake
            lock(); st.connecting = false; if (!ok) st.reconnects++; unlock();
            if (!ok) {
                diagCount(DC_STREAM_FAIL);
                diagEvent("Povezava na tok ni uspela, nov poskus čez %lu s", (unsigned long)(backoff / 1000));
                nextTry = millis() + backoff;
                backoff = min<uint32_t>(backoff * 2, 30000);
            } else {
                backoff = 2000;
            }
        }

        { AMsg x; while (xQueueReceive(aq, &x, 0) == pdTRUE) handleAudioMsg(x); }
        audio->loop();

        // mehki prehod glasnosti: dvig ~1 s čez celo lestvico, spust 2x hitreje
        if (millis() - lastFade >= 8) {
            lastFade = millis();
            int target = muted ? 0 : curVol * FADE_K;
            bool decoding = audio->isRunning() && (!announcePending || millis() - playAt > 4000);   // dvig šele, ko dekodiranje res teče
            if (level < target && decoding) setLevel(level + 1);
            else if (level > target) setLevel(level - 2 < target ? target : level - 2);
        }

        {   // VU pogosteje kot ostalo stanje (za gladek merilnik na zaslonu)
            static uint32_t lastVu = 0;
            if (millis() - lastVu >= 50) { lastVu = millis(); uint16_t v = audio->getVUlevel(); lock(); st.vu = v; unlock(); }
        }
        if (millis() - lastStat > 250) {
            lastStat = millis();
            bool run = audio->isRunning();
            uint32_t sz = audio->getInBufferSize();
            lock();
            st.playing = run;
            if (run) {
                st.codec = audio->getCodecname();
                st.sampleRate = audio->getSampleRate();
                if (!st.bitrate) st.bitrate = audio->getBitRate();
                uint32_t filled = audio->inBufferFilled();
                st.bufferPct = sz ? (uint8_t)(100ULL * filled / sz) : 0;
                uint32_t br = st.bitrate ? st.bitrate : 128000;
                st.bufferDs = (uint16_t)min<uint64_t>(65535, (uint64_t)filled * 80 / br);   // bajti*8/br s -> *10
                st.vu = audio->getVUlevel();
            } else {
                st.bufferPct = 0; st.bufferDs = 0; st.vu = 0;
            }
            unlock();
            // tok se je prekinil -> ponovna povezava po zamiku
            if (want && !run && nextTry < millis()) nextTry = millis() + backoff;
            // tihi zastoj: povezava odprta, medpomnilnik pa prazen 10 s -> ponovna povezava
            if (want && run && sz && audio->inBufferFilled() < sz / 100) {
                if (!emptySince) emptySince = millis();
                else if (millis() - emptySince > 10000) {
                    diagCount(DC_STREAM_STALL);
                    diagEvent("Tok je zastal 10 s – ponovna povezava");
                    audio->stopSong();
                    emptySince = 0; nextTry = millis() + 500;
                    lock(); st.reconnects++; st.lastError = "Tok je zastal"; unlock();
                }
            } else emptySince = 0;
        }
        vTaskDelay(1);
    }
}

bool playerBegin(int bck, int lck, int din) {
    mtx = xSemaphoreCreateMutex();
    q   = xQueueCreate(16, sizeof(PMsg));
    aq  = xQueueCreate(24, sizeof(AMsg));
    Audio::audio_info_callback = onAudioInfo;
    audio = new Audio();
    if (!audio->setPinout(bck, lck, din)) {
        log_e("setPinout ni uspel");
    }
    audio->setVolumeSteps(VOL_STEPS * FADE_K);
    audio->setVolume(0);
    audio->setConnectionTimeout(5000, 8000);
    // Audio sklad in dekoderji v PSRAM, opravilo na jedru 1 (WiFi je na jedru 0)
    TaskHandle_t h = nullptr;
    bool ok = xTaskCreatePinnedToCore(audioTask, "audio", 12288, nullptr, 2, &h, 1) == pdPASS;   // kot primeri knjižnice: navadna prioriteta
    diagSetAudioTask(h);
    return ok;
}
