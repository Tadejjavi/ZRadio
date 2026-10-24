#include "flashguard.h"
#include "diag.h"
#include "app.h"
#include "stations.h"
#include "player.h"
#include <LittleFS.h>
#include <WiFi.h>
#include <esp_system.h>
#include <esp_core_dump.h>
#include <esp_heap_caps.h>
#include <stdarg.h>
#include <time.h>
#include <vector>

// ---------------------------------------------------------------------------
// RTC pomnilnik: preživi programski ponovni zagon, sesutje in watchdog,
// ne pa izklopa napajanja (takrat je vsebina naključna -> preverimo magic).
// ---------------------------------------------------------------------------
static constexpr uint32_t RTC_MAGIC = 0x5A524431;   // "ZRD1"
static constexpr size_t   LOG_SZ    = 3072;

struct RtcState {
    uint32_t magic;
    uint32_t uptime;       // s
    uint32_t heap, heapMin;
    uint32_t epoch;
    int16_t  station;
    uint8_t  playing, vol;
    int8_t   rssi;
    char     event[72];    // zadnji dogodek
    uint16_t head;         // krožni dnevnik
    uint16_t len;
    char     log[LOG_SZ];
};
RTC_NOINIT_ATTR static RtcState rtc;

static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
static String   prevLog;             // dnevnik pred tem zagonom
static uint32_t counters[DC_COUNT] = {0};
static TaskHandle_t audioTask = nullptr;
static uint32_t lastSnap = 0, lastCp = 0;
static uint32_t loopStackMin = 0;
static bool     bootTimeSet = false;
static bool     lastBad = false;
static bool     safe = false;
static String   safeWhy;
static uint8_t  bootfails = 0;

struct Checkpoint {
    uint32_t up, heap, heapMin, largest, psram;
    uint16_t stAudio, stLoop;
    int8_t   rssi;
    uint16_t rec, stall, wifi;
};
static std::vector<Checkpoint> cps;     // zadnjih 72 (12 h)
static const char* BOOTS = "/boots.json";
static constexpr int MAX_BOOTS = 12;

// ---------------------------------------------------------------------------
static void logRaw(const char* s) {
    portENTER_CRITICAL(&mux);
    for (const char* p = s; *p; p++) {
        rtc.log[rtc.head] = *p;
        rtc.head = (rtc.head + 1) % LOG_SZ;
        if (rtc.len < LOG_SZ) rtc.len++;
    }
    portEXIT_CRITICAL(&mux);
}

static String ringToString() {
    String s;
    portENTER_CRITICAL(&mux);
    uint16_t len = min<uint16_t>(rtc.len, LOG_SZ), head = rtc.head % LOG_SZ;
    portEXIT_CRITICAL(&mux);
    s.reserve(len + 1);
    uint16_t start = (head + LOG_SZ - len) % LOG_SZ;
    for (uint16_t i = 0; i < len; i++) s += rtc.log[(start + i) % LOG_SZ];
    int nl = s.indexOf('\n');                       // odreži morebitno prekinjeno prvo vrstico
    if (rtc.len >= LOG_SZ && nl >= 0) s.remove(0, nl + 1);
    return s;
}

void diagEvent(const char* fmt, ...) {
    char msg[160];
    va_list ap; va_start(ap, fmt); vsnprintf(msg, sizeof(msg), fmt, ap); va_end(ap);
    char line[200];
    uint32_t s = millis() / 1000;
    if (appTimeValid()) {
        time_t now = time(nullptr); struct tm tm; localtime_r(&now, &tm);
        snprintf(line, sizeof(line), "%02d:%02d:%02d [%lu] %s\n", tm.tm_hour, tm.tm_min, tm.tm_sec, (unsigned long)s, msg);
    } else {
        snprintf(line, sizeof(line), "--:--:-- [%lu] %s\n", (unsigned long)s, msg);
    }
    logRaw(line);
    portENTER_CRITICAL(&mux);
    strlcpy(rtc.event, msg, sizeof(rtc.event));
    portEXIT_CRITICAL(&mux);
    Serial.print(line);
}

void diagCount(DiagCounter c) { if (c < DC_COUNT) counters[c]++; }
void diagSetAudioTask(TaskHandle_t h) { audioTask = h; }
bool diagLastBootBad() { return lastBad; }

// ---------------------------------------------------------------------------
// Razlog zagona
// ---------------------------------------------------------------------------
struct ReasonInfo { const char* code; const char* title; uint8_t sev; const char* hint; };

static ReasonInfo reasonInfo(esp_reset_reason_t r) {
    switch (r) {
        case ESP_RST_POWERON:  return {"POWERON", "Normalen vklop", 0, ""};
        case ESP_RST_SW:       return {"SW", "Ponovni zagon (programski)", 0, ""};
        case ESP_RST_EXT:      return {"EXT", "Tipka RST / EN", 0, ""};
        case ESP_RST_DEEPSLEEP:return {"DEEPSLEEP", "Prebujanje iz spanja", 0, ""};
        case ESP_RST_USB:      return {"USB", "Ponovni zagon prek USB", 0, ""};
        case ESP_RST_BROWNOUT: return {"BROWNOUT", "Padec napetosti", 2,
            "Napajanje je padlo pod ~2,5 V. Preveri napajalnik (vsaj 5 V / 2 A), kabel USB in kondenzator ob ojačevalniku. "
            "Pogosto se zgodi pri večji glasnosti."};
        case ESP_RST_PANIC:    return {"PANIC", "Sesutje programa", 2,
            "Program je naletel na napako. Spodaj je povzetek (opravilo, vzrok, sklad). Pošlji ga razvijalcu."};
        case ESP_RST_INT_WDT:  return {"INT_WDT", "Zamrznitev (watchdog prekinitev)", 2,
            "Prekinitve so bile predolgo onemogočene. Pogosto zaradi motenj ali napake v gonilniku."};
        case ESP_RST_TASK_WDT: return {"TASK_WDT", "Zamrznitev opravila (watchdog)", 2,
            "Eno od opravil se ni odzivalo več sekund (npr. viseča povezava ali neskončna zanka)."};
        case ESP_RST_WDT:      return {"WDT", "Watchdog", 2, "Strojni watchdog je ponovno zagnal čip."};
        default:               return {"OTHER", "Neznan razlog", 1, ""};
    }
}

static const char* excCause(uint32_t c) {
    switch (c) {
        case 0:  return "IllegalInstruction";
        case 2:  return "InstructionFetchError";
        case 3:  return "LoadStoreError";
        case 4:  return "Level1Interrupt";
        case 6:  return "IntegerDivideByZero";
        case 9:  return "LoadStoreAlignment";
        case 20: return "InstFetchProhibited";
        case 28: return "LoadProhibited (branje neveljavnega naslova)";
        case 29: return "StoreProhibited (pisanje na neveljaven naslov)";
        default: return "";
    }
}

// ---------------------------------------------------------------------------
static void loadBoots(JsonDocument& d) {
    File f = LittleFS.open(BOOTS, "r");
    if (f) { if (deserializeJson(d, f)) d.clear(); f.close(); }
    if (!d.is<JsonArray>()) d.to<JsonArray>();
}
static void saveBoots(JsonDocument& d) {
    FlashGuard g("zagoni");
    File f = LittleFS.open("/boots.tmp", "w");
    if (!f) return;
    serializeJson(d, f); f.close();
    LittleFS.remove(BOOTS); LittleFS.rename("/boots.tmp", BOOTS);
}

void diagBegin() {
    esp_reset_reason_t rr = esp_reset_reason();
    ReasonInfo ri = reasonInfo(rr);
    bool rtcValid = rtc.magic == RTC_MAGIC && rr != ESP_RST_POWERON;

    JsonDocument rec;
    rec["reason"] = ri.code; rec["title"] = ri.title; rec["sev"] = ri.sev; rec["hint"] = ri.hint;
    rec["fw"] = ZR_VERSION;
    rec["heap"] = ESP.getFreeHeap();

    if (rtcValid) {
        JsonObject p = rec["prev"].to<JsonObject>();
        p["up"] = rtc.uptime; p["heap"] = rtc.heap; p["heap_min"] = rtc.heapMin;
        p["station"] = rtc.station; p["playing"] = rtc.playing; p["vol"] = rtc.vol;
        p["rssi"] = rtc.rssi; p["epoch"] = rtc.epoch;
        rtc.event[sizeof(rtc.event) - 1] = 0;
        p["event"] = rtc.event;
        if (rtc.len > LOG_SZ) rtc.len = LOG_SZ;
        rtc.head %= LOG_SZ;
        prevLog = ringToString();
    }

    // povzetek sesutja (coredump v flashu)
    if (esp_core_dump_image_check() == ESP_OK) {
        esp_core_dump_summary_t* s = (esp_core_dump_summary_t*)malloc(sizeof(esp_core_dump_summary_t));
        if (s && esp_core_dump_get_summary(s) == ESP_OK) {
            JsonObject c = rec["crash"].to<JsonObject>();
            s->exc_task[sizeof(s->exc_task) - 1] = 0;
            c["task"] = s->exc_task;
            char b[12];
            snprintf(b, sizeof(b), "0x%08lx", (unsigned long)s->exc_pc); c["pc"] = b;
            c["cause"] = s->ex_info.exc_cause;
            c["cause_txt"] = excCause(s->ex_info.exc_cause);
            snprintf(b, sizeof(b), "0x%08lx", (unsigned long)s->ex_info.exc_vaddr); c["vaddr"] = b;
            String bt;
            for (uint32_t i = 0; i < s->exc_bt_info.depth && i < 16; i++) {
                snprintf(b, sizeof(b), "0x%08lx", (unsigned long)s->exc_bt_info.bt[i]);
                if (i) bt += ' ';
                bt += b;
            }
            c["bt"] = bt;
            c["bt_corrupted"] = s->exc_bt_info.corrupted;
            char sha[17]; memcpy(sha, s->app_elf_sha256, 16); sha[16] = 0; c["elf"] = sha;
        }
        free(s);
        esp_core_dump_image_erase();     // isti povzetek ne pripnemo dvakrat
    }

    lastBad = ri.sev >= 2;

    JsonDocument d;
    loadBoots(d);
    JsonArray a = d.as<JsonArray>();
    a.add(rec);
    while (a.size() > MAX_BOOTS) a.remove(0);
    saveBoots(d);

    // nov dnevnik za to sejo
    memset(&rtc, 0, sizeof(rtc));
    rtc.magic = RTC_MAGIC;
    diagEvent("Zagon: %s, fw %s", ri.title, ZR_VERSION);

    WiFi.onEvent([](WiFiEvent_t e, WiFiEventInfo_t info) {
        if (e == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
            diagCount(DC_WIFI_DISC);
            diagEvent("WiFi prekinjen (razlog %d)", info.wifi_sta_disconnected.reason);
        } else if (e == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
            diagEvent("WiFi povezan, IP %s, %d dBm", WiFi.localIP().toString().c_str(), WiFi.RSSI());
        }
    });
}

void diagSetSafe(bool s, const char* why, uint8_t fails) {
    safe = s; safeWhy = why ? why : ""; bootfails = fails;
    if (s) diagEvent("VARNI NAČIN: %s", why);
}

// ---------------------------------------------------------------------------
static uint16_t stackFree(TaskHandle_t h) { return h ? uxTaskGetStackHighWaterMark(h) : 0; }

void diagLoop() {
    uint32_t now = millis();
    uint32_t lm = uxTaskGetStackHighWaterMark(nullptr);
    if (!loopStackMin || lm < loopStackMin) loopStackMin = lm;

    // samopopravilo: če notranji RAM 60 s ostane kritično nizek (WiFi/lwIP ne dobita pomnilnika,
    // radio pa ostane živ in nedosegljiv) ali WiFi 10 min ni povezan -> zapiši in ponovno zaženi
    {
        static uint32_t lowSince = 0, wifiDownSince = 0, lastCheck = 0;
        if (now - lastCheck >= 1000) {
            lastCheck = now;
            size_t freeInt = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            bool low = freeInt < 20 * 1024 || largest < 8 * 1024;
            if (!low) lowSince = 0;
            else if (!lowSince) { lowSince = now; diagEvent("Opozorilo: malo notranjega RAM-a (%u B, največji kos %u B)", (unsigned)freeInt, (unsigned)largest); }
            else if (now - lowSince > 60000) { diagEvent("Samopopravilo: notranji RAM 60 s prenizek – ponovni zagon"); delay(200); ESP.restart(); }
            bool up = WiFi.status() == WL_CONNECTED || WiFi.getMode() == WIFI_AP;
            if (up) wifiDownSince = 0;
            else if (!wifiDownSince) wifiDownSince = now;
            else if (now - wifiDownSince > 600000) { diagEvent("Samopopravilo: WiFi 10 min brez povezave – ponovni zagon"); delay(200); ESP.restart(); }
        }
    }

    if (now - lastSnap >= 1000) {                   // posnetek stanja v RTC (za primer sesutja)
        lastSnap = now;
        // vrednosti najprej preberi IZVEN kritične sekcije – WiFi.RSSI() in podobni klici
        // čakajo na druga opravila in v kritični sekciji (prekinitve izklopljene) sprožijo watchdog
        PlayerStatus ps = playerStatus();
        uint32_t heap = ESP.getFreeHeap(), heapMin = ESP.getMinFreeHeap();
        int8_t   rssi = WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0;
        uint32_t epoch = appTimeValid() ? time(nullptr) : 0;
        int16_t  st = ui.cur;
        uint8_t  vol = ui.vol;
        portENTER_CRITICAL(&mux);
        rtc.uptime = now / 1000;
        rtc.heap = heap; rtc.heapMin = heapMin;
        rtc.station = st; rtc.playing = ps.playing; rtc.vol = vol;
        rtc.rssi = rssi; rtc.epoch = epoch;
        portEXIT_CRITICAL(&mux);

        if (!bootTimeSet && appTimeValid()) {        // čas zagona vpišemo, ko je NTP na voljo
            bootTimeSet = true;
            JsonDocument d; loadBoots(d);
            JsonArray a = d.as<JsonArray>();
            if (a.size()) { a[a.size() - 1]["epoch"] = (uint32_t)(time(nullptr) - now / 1000); saveBoots(d); }
        }
    }

    // kontrolna točka po 1 min, nato vsakih 10 min
    if ((cps.empty() && now > 60000) || (!cps.empty() && now - lastCp >= 600000)) {
        lastCp = now;
        PlayerStatus ps = playerStatus();
        Checkpoint c;
        c.up = now / 1000;
        c.heap = ESP.getFreeHeap(); c.heapMin = ESP.getMinFreeHeap();
        c.largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        c.psram = ESP.getFreePsram();
        c.stAudio = stackFree(audioTask); c.stLoop = loopStackMin;
        c.rssi = WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0;
        c.rec = ps.reconnects; c.stall = counters[DC_STREAM_STALL]; c.wifi = counters[DC_WIFI_DISC];
        cps.push_back(c);
        if (cps.size() > 72) cps.erase(cps.begin());
    }
}

// ---------------------------------------------------------------------------
void diagToJson(JsonObject o) {
    PlayerStatus ps = playerStatus();
    JsonObject n = o["now"].to<JsonObject>();
    n["up"] = millis() / 1000;
    n["heap"] = ESP.getFreeHeap(); n["heap_min"] = ESP.getMinFreeHeap();
    n["largest"] = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    n["psram"] = ESP.getFreePsram(); n["psram_min"] = ESP.getMinFreePsram();
    n["st_audio"] = stackFree(audioTask);
    n["st_loop"] = loopStackMin;
    n["st_web"] = stackFree(xTaskGetHandle("async_tcp"));
    n["st_dec"] = playerDecoderStackFree();
    n["flash_max"] = flashGuardMaxMs(); n["flash_n"] = flashGuardCount();
    n["temp"] = temperatureRead();
    n["rssi"] = WiFi.RSSI();
    n["safe"] = safe; n["safe_why"] = safeWhy; n["bootfails"] = bootfails;
    JsonObject c = n["cnt"].to<JsonObject>();
    c["reconnects"] = ps.reconnects;
    c["stream_fail"] = counters[DC_STREAM_FAIL];
    c["stall"] = counters[DC_STREAM_STALL];
    c["wifi"] = counters[DC_WIFI_DISC];
    c["oled_reinit"] = counters[DC_OLED_REINIT];
    c["oled_lost"] = counters[DC_OLED_LOST];

    JsonDocument b; loadBoots(b);
    o["boots"] = b.as<JsonArray>();

    JsonArray ca = o["cp"].to<JsonArray>();
    for (auto& x : cps) {
        JsonArray r = ca.add<JsonArray>();
        r.add(x.up); r.add(x.heap); r.add(x.heapMin); r.add(x.largest); r.add(x.psram);
        r.add(x.stAudio); r.add(x.stLoop); r.add(x.rssi); r.add(x.rec); r.add(x.stall); r.add(x.wifi);
    }
    o["log"] = ringToString();
    o["prevlog"] = prevLog;
}

String diagLogText() {
    String s = "=== ZRadio " ZR_VERSION " – dnevnik ===\n";
    if (prevLog.length()) s += "\n--- Pred zadnjim ponovnim zagonom ---\n" + prevLog;
    s += "\n--- Trenutna seja ---\n" + ringToString();
    return s;
}

void diagClear() {
    LittleFS.remove(BOOTS);
    cps.clear();
    prevLog = "";
    for (auto& c : counters) c = 0;
    diagEvent("Diagnostika počiščena");
}
