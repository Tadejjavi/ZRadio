#include "display_tft.h"
#include "logo.h"
#include "display.h"
#include "flashguard.h"
#include "web.h"
#include "app.h"
#include "config.h"
#include "player.h"
#include "stations.h"
#include "input.h"
#include "diag.h"
#include "web_index.h"      // ustvari tools/embed_web.py iz web/index.html
#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include <DNSServer.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <Update.h>
#include <WiFi.h>
#include <esp_system.h>

static AsyncWebServer server(80);
static DNSServer      dns;
static bool           ap = false;
static uint32_t       rebootAt = 0;
static bool           otaOk = false;
static String         otaErr;

static void sendJson(AsyncWebServerRequest* r, JsonDocument& d, int code = 200) {
    String s; serializeJson(d, s);
    r->send(code, "application/json", s);
}
static void sendOk(AsyncWebServerRequest* r) { r->send(200, "application/json", "{\"ok\":true}"); }
static void sendErr(AsyncWebServerRequest* r, const String& msg, int code = 400) {
    JsonDocument d; d["ok"] = false; d["error"] = msg; sendJson(r, d, code);
}

static const char* resetReason() {
    switch (esp_reset_reason()) {
        case ESP_RST_POWERON:  return "vklop";
        case ESP_RST_SW:       return "programski";
        case ESP_RST_PANIC:    return "sesutje (panic)";
        case ESP_RST_INT_WDT:  return "watchdog (prekinitve)";
        case ESP_RST_TASK_WDT: return "watchdog (opravilo)";
        case ESP_RST_WDT:      return "watchdog";
        case ESP_RST_BROWNOUT: return "padec napetosti";
        case ESP_RST_DEEPSLEEP:return "globoko spanje";
        case ESP_RST_EXT:      return "zunanji (EN)";
        default:               return "neznano";
    }
}

// ---------------------------------------------------------------------------
static void apiStatus(AsyncWebServerRequest* r) {
    JsonDocument d;
    PlayerStatus ps = playerStatus();
    appLock();
    d["st"] = ui.cur;
    d["name"] = (ui.cur >= 0 && ui.cur < (int)stations.size()) ? stations[ui.cur].name : String("");
    d["vol"] = ui.vol; d["max"] = cfg.vol_limit; d["mute"] = ui.mute;
    d["standby"] = ui.standby; d["fav"] = ui.favOnly; d["n"] = stations.size();
    appUnlock();
    d["title"] = ps.title; d["icy"] = ps.icyName; d["codec"] = ps.codec;
    d["br"] = ps.bitrate; d["sr"] = ps.sampleRate; d["buf"] = ps.bufferPct; d["buf_s"] = ps.bufferDs / 10.0;
    d["playing"] = ps.playing; d["wanted"] = ps.wanted; d["conn"] = ps.connecting;
    d["rec"] = ps.reconnects; d["err"] = ps.lastError;
    d["vu"] = ps.vu;
    d["rssi"] = WiFi.RSSI();
    d["ir_seq"] = irSeq();
    char hex[11]; snprintf(hex, sizeof(hex), "0x%08lX", (unsigned long)irLastCode());
    d["ir"] = hex;
    if (appTimeValid()) {
        time_t now = time(nullptr); struct tm tm; localtime_r(&now, &tm);
        char b[9]; strftime(b, sizeof(b), "%H:%M", &tm); d["time"] = b;
    }
    sendJson(r, d);
}

static void apiCmd(AsyncWebServerRequest* r) {
    if (r->hasParam("a")) {
        Action a = actionFromId(r->getParam("a")->value().c_str());
        if (a == ACT_NONE) return sendErr(r, "Neznana akcija");
        appPost(AC_ACTION, a);
    } else if (r->hasParam("play")) {
        appPost(AC_PLAY, r->getParam("play")->value().toInt());
    } else if (r->hasParam("vol")) {
        appPost(AC_VOL, r->getParam("vol")->value().toInt());
    } else return sendErr(r, "Manjka parameter");
    sendOk(r);
}

static void apiInfo(AsyncWebServerRequest* r) {
    JsonDocument d;
    d["version"] = ZR_VERSION;
    d["uptime"] = millis() / 1000;
    d["heap"] = ESP.getFreeHeap(); d["heap_min"] = ESP.getMinFreeHeap();
    d["psram"] = ESP.getFreePsram(); d["psram_total"] = ESP.getPsramSize();
    d["flash"] = ESP.getFlashChipSize();
    d["sketch"] = ESP.getSketchSize(); d["sketch_free"] = ESP.getFreeSketchSpace();
    d["fs_used"] = LittleFS.usedBytes(); d["fs_total"] = LittleFS.totalBytes();
    d["chip"] = ESP.getChipModel(); d["rev"] = ESP.getChipRevision();
    d["temp"] = temperatureRead();
    d["reset"] = resetReason();
    d["safe"] = safeMode; d["ap"] = ap; d["last_bad"] = diagLastBootBad();
    d["mac"] = WiFi.macAddress();
    d["ip"] = ap ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
    d["ssid"] = WiFi.SSID(); d["rssi"] = WiFi.RSSI();
    d["host"] = cfg.hostname;
    sendJson(r, d);
}

static void apiPins(AsyncWebServerRequest* r) {
    JsonDocument d;
    JsonArray a = d["pins"].to<JsonArray>();
    for (int g = 0; g <= 48; g++) {
        PinClass c = pinClass(g);
        if (c == PIN_NA) continue;
        JsonObject o = a.add<JsonObject>();
        o["g"] = g; o["c"] = (int)c; o["n"] = pinNote(g);
    }
    sendJson(r, d);
}

static void apiGetConfig(AsyncWebServerRequest* r) {
    JsonDocument d;
    appLock(); cfg.toJson(d.to<JsonObject>(), false); appUnlock();
    sendJson(r, d);
}

static void handleConfig(AsyncWebServerRequest* r, JsonVariant& json, bool save) {
    if (!json.is<JsonObject>()) return sendErr(r, "Pričakovan JSON objekt");
    appLock(); Config tmp = cfg; appUnlock();
    tmp.fromJson(json.as<JsonObjectConst>());
    std::vector<String> err, warn;
    bool ok = validateConfig(tmp, err, warn);
    JsonDocument d;
    d["ok"] = ok;
    JsonArray e = d["errors"].to<JsonArray>();   for (auto& s : err)  e.add(s);
    JsonArray w = d["warnings"].to<JsonArray>(); for (auto& s : warn) w.add(s);
    if (ok && save) {
        appLock();
        bool reboot = needsReboot(cfg, tmp);
        cfg = tmp;
        bool saved = saveConfig(cfg);
        diagEvent("Nastavitve shranjene%s", reboot ? " (potreben ponovni zagon)" : "");
        appUnlock();
        d["reboot"] = reboot;
        d["saved"] = saved;
        if (!saved) d["ok"] = false, e.add("Shranjevanje na LittleFS ni uspelo");
        appPost(AC_APPLY_CONFIG);
    }
    sendJson(r, d, ok ? 200 : 400);
}

static void handleStations(AsyncWebServerRequest* r, JsonVariant& json) {
    std::vector<Station> tmp;
    String err;
    if (!stationsFromJson(json.as<JsonArrayConst>(), tmp, err)) return sendErr(r, err);
    appLock();
    stations.swap(tmp);
    bool ok = saveStations();
    appUnlock();
    appPost(AC_RELOAD_STATIONS);
    if (!ok) return sendErr(r, "Shranjevanje ni uspelo", 500);
    JsonDocument d; d["ok"] = true; d["n"] = stations.size(); sendJson(r, d);
}

static void apiGetStations(AsyncWebServerRequest* r) {
    JsonDocument d;
    appLock(); stationsToJson(d.to<JsonArray>()); appUnlock();
    sendJson(r, d);
}

static void apiBackup(AsyncWebServerRequest* r) {
    JsonDocument d;
    appLock();
    cfg.toJson(d["config"].to<JsonObject>(), true);
    stationsToJson(d["stations"].to<JsonArray>());
    appUnlock();
    d["version"] = ZR_VERSION;
    String s; serializeJsonPretty(d, s);
    AsyncWebServerResponse* res = r->beginResponse(200, "application/json", s);
    res->addHeader("Content-Disposition", "attachment; filename=\"zradio-backup.json\"");
    r->send(res);
}

static void handleRestore(AsyncWebServerRequest* r, JsonVariant& json) {
    JsonObjectConst o = json.as<JsonObjectConst>();
    if (o.isNull() || o["config"].isNull()) return sendErr(r, "Neveljavna varnostna kopija");
    Config tmp;
    tmp.fromJson(o["config"]);
    std::vector<String> err, warn;
    if (!validateConfig(tmp, err, warn)) return sendErr(r, "Nastavitve v kopiji niso veljavne: " + err[0]);
    std::vector<Station> st;
    String e;
    if (!o["stations"].isNull() && !stationsFromJson(o["stations"], st, e)) return sendErr(r, e);
    appLock();
    cfg = tmp; saveConfig(cfg);
    if (!st.empty()) { stations.swap(st); saveStations(); }
    appUnlock();
    rebootAt = millis() + 1500;
    sendOk(r);
}

static void apiScan(AsyncWebServerRequest* r) {
    int n = WiFi.scanComplete();
    JsonDocument d;
    if (n == WIFI_SCAN_FAILED) { WiFi.scanNetworks(true); d["running"] = true; }
    else if (n == WIFI_SCAN_RUNNING) d["running"] = true;
    else {
        JsonArray a = d["nets"].to<JsonArray>();
        for (int i = 0; i < n; i++) {
            JsonObject o = a.add<JsonObject>();
            o["ssid"] = WiFi.SSID(i); o["rssi"] = WiFi.RSSI(i);
            o["lock"] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
        }
        WiFi.scanDelete();
    }
    sendJson(r, d);
}

static void otaUpload(AsyncWebServerRequest* r, const String& fn, size_t index, uint8_t* data, size_t len, bool final) {
    if (index == 0) {
        otaOk = false; otaErr = "";
        playerStop();
        diagEvent("Posodobitev: začetek (%s)", fn.c_str());
        if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) otaErr = Update.errorString();
    }
    if (otaErr.isEmpty() && len && Update.write(data, len) != len) otaErr = Update.errorString();
    if (final && otaErr.isEmpty()) {
        if (Update.end(true)) otaOk = true; else otaErr = Update.errorString();
        diagEvent(otaOk ? "Posodobitev uspešna (%u B)" : "Posodobitev ni uspela: %s", otaOk ? (unsigned)(index + len) : 0, otaErr.c_str());
    }
}

// ---------------------------------------------------------------------------
void webBegin(bool apMode) {
    ap = apMode;
    if (ap) dns.start(53, "*", WiFi.softAPIP());

    server.on("/", HTTP_GET, [](AsyncWebServerRequest* r) {
        AsyncWebServerResponse* res = r->beginResponse(200, "text/html", WEB_INDEX_GZ, WEB_INDEX_GZ_LEN);
        res->addHeader("Content-Encoding", "gzip");
        res->addHeader("Cache-Control", "no-cache");
        r->send(res);
    });
    server.on("/api/status", HTTP_GET, apiStatus);
    server.on("/api/info", HTTP_GET, apiInfo);
    server.on("/api/pins", HTTP_GET, apiPins);
    server.on("/api/cmd", HTTP_POST, apiCmd);
    server.on("/api/input", HTTP_POST, [](AsyncWebServerRequest* r) {   // virtualni enkoder
        int e = r->hasParam("enc") ? r->getParam("enc")->value().toInt() : 0;
        String ev = r->hasParam("ev") ? r->getParam("ev")->value() : "";
        int code = ev == "r" ? 1 : ev == "l" ? 2 : ev == "c" ? 3 : ev == "h" ? 4 : 0;
        if ((e != 1 && e != 2) || !code) { r->send(400, "text/plain", "enc=1|2, ev=r|l|c|h"); return; }
        appPost(AC_INPUT, e * 10 + code);
        sendOk(r);
    });
    server.on("/api/config", HTTP_GET, apiGetConfig);
    server.on("/api/stations", HTTP_GET, apiGetStations);
    server.on("/api/backup", HTTP_GET, apiBackup);
    server.on("/api/wifi/scan", HTTP_GET, apiScan);
    server.on("/api/diag", HTTP_GET, [](AsyncWebServerRequest* r) {
        JsonDocument d; diagToJson(d.to<JsonObject>()); sendJson(r, d);
    });
    server.on("/api/diag/log", HTTP_GET, [](AsyncWebServerRequest* r) {
        AsyncWebServerResponse* res = r->beginResponse(200, "text/plain; charset=utf-8", diagLogText());
        res->addHeader("Content-Disposition", "attachment; filename=\"zradio-dnevnik.txt\"");
        r->send(res);
    });
    server.on("/api/screen", HTTP_GET, [](AsyncWebServerRequest* r) {
        static uint8_t* snap = nullptr;
        const size_t SN = 160 * 120;
        if (!snap) snap = (uint8_t*)ps_malloc(SN);
        uint32_t age = 0; const char* fmt = "";
        size_t n = snap ? displaySnapshot(snap, SN, age, fmt) : 0;
        if (!n) { r->send(204); return; }
        AsyncWebServerResponse* res = r->beginResponse(200, "application/octet-stream", snap, n);
        res->addHeader("X-Age", String(age));
        res->addHeader("X-Fmt", fmt);
        res->addHeader("Cache-Control", "no-store");
        r->send(res);
    });
    // --- slika ozadja (postavitev Obroči): 320x240 RGB565 = 153600 B ---
    server.on("/api/bg", HTTP_POST,
        [](AsyncWebServerRequest* r) {
            uint8_t* b = (uint8_t*)r->_tempObject;
            bool ok = false;
            if (b && r->contentLength() == 153600) {
                FlashGuard g("ozadje");
                File f = LittleFS.open("/bg.tmp", "w");
                if (f) { ok = f.write(b, 153600) == 153600; f.close(); }
                if (ok) { LittleFS.remove("/bg.565"); ok = LittleFS.rename("/bg.tmp", "/bg.565"); }
            }
            if (ok) { tftBgChanged(); sendOk(r); } else sendErr(r, "ozadje ni bilo shranjeno (pričakovanih 153600 B RGB565)", 400);
        }, nullptr,
        [](AsyncWebServerRequest* r, uint8_t* data, size_t len, size_t index, size_t total) {
            if (total != 153600) return;
            if (index == 0) r->_tempObject = ps_malloc(153600);
            if (r->_tempObject && index + len <= 153600) memcpy((uint8_t*)r->_tempObject + index, data, len);
        });
    // --- logotipi postaj ---
    server.on("/api/logo/status", HTTP_GET, [](AsyncWebServerRequest* r) {
        int p = 0; String last; logoStatus(p, last);
        JsonDocument d; d["pending"] = p; d["last"] = last; d["raw_done"] = logoRawDone(); sendJson(r, d);
    });
    server.on("/api/logo/get", HTTP_POST, [](AsyncWebServerRequest* r) {
        uint32_t id = r->hasParam("src") ? logoGetRaw(r->getParam("src")->value()) : 0;
        if (!id) { sendErr(r, "vrsta je polna ali neveljaven URL", 503); return; }
        JsonDocument d; d["id"] = id; sendJson(r, d);
    });
    server.on("/api/logo/raw", HTTP_GET, [](AsyncWebServerRequest* r) {
        size_t n = 0; const uint8_t* b = logoRaw(n);
        if (!b || !n) { r->send(404); return; }
        AsyncWebServerResponse* res = r->beginResponse(200, "application/octet-stream", b, n);
        res->addHeader("Cache-Control", "no-store");
        r->send(res);
    });
    server.on("/api/logo/fetch", HTTP_POST, [](AsyncWebServerRequest* r) {
        if (!r->hasParam("u") || !r->hasParam("src")) { sendErr(r, "manjka u ali src", 400); return; }
        if (!logoFetch(r->getParam("u")->value(), r->getParam("src")->value())) { sendErr(r, "vrsta je polna ali neveljaven URL", 503); return; }
        sendOk(r);
    });
    server.on("/api/logo", HTTP_GET, [](AsyncWebServerRequest* r) {
        if (!r->hasParam("u")) { r->send(400); return; }
        String p = logoPath(r->getParam("u")->value());
        if (!LittleFS.exists(p)) { r->send(404); return; }
        AsyncWebServerResponse* res = r->beginResponse(LittleFS, p, "application/octet-stream");
        res->addHeader("Cache-Control", "no-store");
        r->send(res);
    });
    server.on("/api/logo", HTTP_DELETE, [](AsyncWebServerRequest* r) {
        if (!r->hasParam("u")) { r->send(400); return; }
        logoDelete(r->getParam("u")->value()); sendOk(r);
    });
    server.on("/api/logo", HTTP_POST,
        [](AsyncWebServerRequest* r) {
            uint8_t* b = (uint8_t*)r->_tempObject;
            bool ok = b && r->hasParam("u") && r->contentLength() == LOGO_BYTES && logoSave(r->getParam("u")->value(), b, LOGO_BYTES);
            if (ok) sendOk(r); else sendErr(r, "logotip ni bil shranjen (pričakovanih 8192 B RGB565)", 400);
        }, nullptr,
        [](AsyncWebServerRequest* r, uint8_t* data, size_t len, size_t index, size_t total) {
            if (total != LOGO_BYTES) return;
            if (index == 0) r->_tempObject = ps_malloc(LOGO_BYTES);   // sprosti knjižnica ob koncu zahteve
            if (r->_tempObject && index + len <= LOGO_BYTES) memcpy((uint8_t*)r->_tempObject + index, data, len);
        });
    server.on("/api/diag/clear", HTTP_POST, [](AsyncWebServerRequest* r) { diagClear(); sendOk(r); });
    server.on("/api/reboot", HTTP_POST, [](AsyncWebServerRequest* r) { rebootAt = millis() + 800; sendOk(r); });
    server.on("/api/factory", HTTP_POST, [](AsyncWebServerRequest* r) {
        diagEvent("Tovarniške nastavitve");
        LittleFS.remove("/config.json"); LittleFS.remove("/stations.json"); LittleFS.remove("/boots.json");
        Preferences p; p.begin("rt", false); p.clear(); p.end();
        rebootAt = millis() + 800; sendOk(r);
    });
    server.on("/api/ota", HTTP_POST,
        [](AsyncWebServerRequest* r) {
            if (otaOk) { rebootAt = millis() + 1500; sendOk(r); }
            else sendErr(r, otaErr.length() ? otaErr : String("Nalaganje ni uspelo"), 500);
        }, otaUpload);

    auto* hCfg = new AsyncCallbackJsonWebHandler("/api/config",
        [](AsyncWebServerRequest* r, JsonVariant& j) { handleConfig(r, j, true); });
    hCfg->setMethod(HTTP_POST); hCfg->setMaxContentLength(16384);
    server.addHandler(hCfg);

    auto* hVal = new AsyncCallbackJsonWebHandler("/api/config/validate",
        [](AsyncWebServerRequest* r, JsonVariant& j) { handleConfig(r, j, false); });
    hVal->setMethod(HTTP_POST); hVal->setMaxContentLength(16384);
    server.addHandler(hVal);

    auto* hSt = new AsyncCallbackJsonWebHandler("/api/stations", handleStations);
    hSt->setMethod(HTTP_POST); hSt->setMaxContentLength(96 * 1024);
    server.addHandler(hSt);

    auto* hRs = new AsyncCallbackJsonWebHandler("/api/restore", handleRestore);
    hRs->setMethod(HTTP_POST); hRs->setMaxContentLength(128 * 1024);
    server.addHandler(hRs);

    server.onNotFound([](AsyncWebServerRequest* r) {
        if (ap) r->redirect("http://" + WiFi.softAPIP().toString() + "/");   // captive portal
        else r->send(404, "text/plain", "Ni najdeno");
    });
    server.begin();
}

void webLoop() {
    if (ap) dns.processNextRequest();
    if (rebootAt && millis() > rebootAt) { rebootAt = 0; appPost(AC_REBOOT); }
}
