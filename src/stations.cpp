#include "flashguard.h"
#include "stations.h"
#include <LittleFS.h>

std::vector<Station> stations;
static const char* ST_PATH = "/stations.json";

void defaultStations() {
    stations = {
        {"Val 202",       "http://mp3.rtvslo.si/val202",     "Pop",    true},
        {"Radio Prvi",    "http://mp3.rtvslo.si/ra1",        "Informativni", false},
        {"Ars",           "http://mp3.rtvslo.si/ars",        "Klasika", false},
        {"Radio 1",       "http://live.radio.si/Radio1",     "Pop",    true},
        {"Radio Antena",  "http://live.radio.si/Antena",     "Pop",    false},
        {"BestFM",        "http://live.radio.si/BestFM",     "Ex-Yu",  false},
        {"Radio Aktual",  "http://195.95.158.131:9080/",     "Ex-Yu",  false},
        {"Rock Radio",    "http://stream.rockradio.si:9034/","Rock",   false},
        {"Radio Krka",    "http://live.radio.si/Krka",       "Regionalni", false},
    };
}

void stationsToJson(JsonArray a) {
    for (auto& s : stations) {
        JsonObject o = a.add<JsonObject>();
        o["n"] = s.name; o["u"] = s.url; o["g"] = s.genre; o["f"] = s.fav;
    }
}

bool stationsFromJson(JsonArrayConst a, std::vector<Station>& out, String& err) {
    out.clear();
    if (a.isNull()) { err = "Pričakovan seznam postaj"; return false; }
    for (JsonObjectConst o : a) {
        Station s;
        s.name  = o["n"] | "";
        s.url   = o["u"] | "";
        s.genre = o["g"] | "";
        s.fav   = o["f"] | false;
        s.name.trim(); s.url.trim();
        if (s.url.isEmpty()) continue;
        if (!s.url.startsWith("http://") && !s.url.startsWith("https://")) {
            err = "Neveljaven URL: " + s.url; return false;
        }
        if (s.name.isEmpty()) s.name = s.url;
        if ((int)out.size() >= MAX_STATIONS) { err = "Največ " + String(MAX_STATIONS) + " postaj"; return false; }
        out.push_back(s);
    }
    return true;
}

bool loadStations() {
    File f = LittleFS.open(ST_PATH, "r");
    if (!f) return false;
    JsonDocument doc;
    auto e = deserializeJson(doc, f);
    f.close();
    if (e) return false;
    String err;
    std::vector<Station> tmp;
    if (!stationsFromJson(doc.as<JsonArrayConst>(), tmp, err)) return false;
    stations.swap(tmp);
    return true;
}

bool saveStations() {
    FlashGuard g("postaje");
    JsonDocument doc;
    stationsToJson(doc.to<JsonArray>());
    File f = LittleFS.open("/stations.tmp", "w");
    if (!f) return false;
    size_t n = serializeJson(doc, f);
    f.close();
    if (!n) return false;
    LittleFS.remove(ST_PATH);
    return LittleFS.rename("/stations.tmp", ST_PATH);
}
