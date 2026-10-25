#include "flashguard.h"
#include "config.h"
#include <LittleFS.h>

Config cfg;

const char* const ACTION_IDS[ACT_COUNT] = {
    "none", "vol_up", "vol_down", "next", "prev", "play_stop",
    "mute", "power", "fav", "browse", "ok",
    "menu", "ch_prev", "ch_next", "eq", "sleep",
    "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7", "d8", "d9"
};

Action actionFromId(const char* id) {
    if (!id) return ACT_NONE;
    for (int i = 0; i < ACT_COUNT; i++)
        if (strcmp(id, ACTION_IDS[i]) == 0) return (Action)i;
    return ACT_NONE;
}

Config::Config() {
    enc1.en = true;  enc1.a = 4;  enc1.b = 5;  enc1.sw = 6;
    enc2.en = false; enc2.a = 16; enc2.b = 17; enc2.sw = 18;
}

// ---------------------------------------------------------------------------
// JSON
// ---------------------------------------------------------------------------
static void encToJson(JsonObject o, const EncCfg& e) {
    o["en"] = e.en; o["a"] = e.a; o["b"] = e.b; o["sw"] = e.sw;
    o["steps"] = e.steps; o["invert"] = e.invert;
}
static void encFromJson(JsonObjectConst o, EncCfg& e) {
    if (o.isNull()) return;
    e.en     = o["en"]     | e.en;
    e.a      = o["a"]      | e.a;
    e.b      = o["b"]      | e.b;
    e.sw     = o["sw"]     | e.sw;
    e.steps  = constrain((int)(o["steps"] | (int)e.steps), 1, 8);
    e.invert = o["invert"] | e.invert;
}

void Config::toJson(JsonObject o, bool withSecrets) const {
    JsonObject n = o["net"].to<JsonObject>();
    n["hostname"] = hostname; n["ssid"] = ssid;
    n["pass"] = withSecrets ? pass : (pass.length() ? String("********") : String(""));
    n["tz"] = tz; n["ntp"] = ntp;

    JsonObject a = o["audio"].to<JsonObject>();
    a["bck"] = i2s_bck; a["lck"] = i2s_lck; a["din"] = i2s_din;
    a["amp_en"] = amp_en; a["amp_inv"] = amp_inv;
    a["vol_limit"] = vol_limit;
    a["eq_low"] = eq_low; a["eq_mid"] = eq_mid; a["eq_high"] = eq_high;
    a["mono"] = mono; a["autoplay"] = autoplay;

    JsonObject d = o["display"].to<JsonObject>();
    d["type"] = disp; d["sda"] = sda; d["scl"] = scl; d["rst"] = disp_rst;
    d["sck"] = spi_sck; d["mosi"] = spi_mosi; d["cs"] = spi_cs; d["dc"] = spi_dc; d["bl"] = bl; d["spi_mhz"] = spi_mhz;
    d["addr"] = i2c_addr; d["rot180"] = rot180;
    d["contrast"] = contrast; d["contrast_night"] = contrast_night;
    d["night_from"] = night_from; d["night_to"] = night_to;
    d["saver_min"] = saver_min; d["standby_clock"] = standby_clock; d["refresh_min"] = refresh_min;
    d["viz"] = viz; d["theme"] = theme; d["layout"] = layout; d["night_theme"] = night_theme; d["bg_kind"] = bg_kind; d["ring_mode"] = ring_mode;

    JsonObject in = o["input"].to<JsonObject>();
    encToJson(in["enc1"].to<JsonObject>(), enc1);
    encToJson(in["enc2"].to<JsonObject>(), enc2);
    in["ir"] = ir;
    JsonObject codes = in["ir_codes"].to<JsonObject>();
    for (int i = 1; i < ACT_COUNT; i++) {
        if (ir_codes[i]) {
            char hex[11]; snprintf(hex, sizeof(hex), "0x%08lX", (unsigned long)ir_codes[i]);
            codes[ACTION_IDS[i]] = hex;
        }
    }
    JsonArray bt = in["buttons"].to<JsonArray>();
    for (int i = 0; i < MAX_BUTTONS; i++) {
        JsonObject b = bt.add<JsonObject>();
        b["pin"] = btn[i].pin; b["action"] = ACTION_IDS[btn[i].action];
    }
}

void Config::fromJson(JsonObjectConst o) {
    JsonObjectConst n = o["net"];
    if (!n.isNull()) {
        hostname = n["hostname"] | hostname.c_str();
        ssid     = n["ssid"]     | ssid.c_str();
        const char* p = n["pass"] | (const char*)nullptr;
        if (p && strcmp(p, "********") != 0) pass = p;   // maskirano geslo = ne spreminjaj
        tz  = n["tz"]  | tz.c_str();
        ntp = n["ntp"] | ntp.c_str();
    }
    JsonObjectConst a = o["audio"];
    if (!a.isNull()) {
        i2s_bck = a["bck"] | i2s_bck; i2s_lck = a["lck"] | i2s_lck; i2s_din = a["din"] | i2s_din;
        amp_en  = a["amp_en"] | amp_en; amp_inv = a["amp_inv"] | amp_inv;
        vol_limit = constrain((int)(a["vol_limit"] | (int)vol_limit), 1, VOL_STEPS);
        eq_low  = constrain((int)(a["eq_low"]  | (int)eq_low),  -12, 12);
        eq_mid  = constrain((int)(a["eq_mid"]  | (int)eq_mid),  -12, 12);
        eq_high = constrain((int)(a["eq_high"] | (int)eq_high), -12, 12);
        mono = a["mono"] | mono; autoplay = a["autoplay"] | autoplay;
    }
    JsonObjectConst d = o["display"];
    if (!d.isNull()) {
        disp = d["type"] | disp.c_str();
        sda = d["sda"] | sda; scl = d["scl"] | scl; disp_rst = d["rst"] | disp_rst;
        spi_sck = d["sck"] | spi_sck; spi_mosi = d["mosi"] | spi_mosi; spi_cs = d["cs"] | spi_cs;
        spi_dc = d["dc"] | spi_dc; bl = d["bl"] | bl;
        spi_mhz = (int)(d["spi_mhz"] | (int)spi_mhz) >= 80 ? 80 : 40;
        i2c_addr = d["addr"] | i2c_addr; rot180 = d["rot180"] | rot180;
        contrast = d["contrast"] | contrast; contrast_night = d["contrast_night"] | contrast_night;
        night_from = constrain((int)(d["night_from"] | (int)night_from), 0, 23);
        night_to   = constrain((int)(d["night_to"]   | (int)night_to),   0, 23);
        saver_min = d["saver_min"] | saver_min; standby_clock = d["standby_clock"] | standby_clock;
        refresh_min = d["refresh_min"] | refresh_min;
        viz = d["viz"] | viz.c_str();
        if (viz != "vu" && viz != "spectrum" && viz != "analog" && viz != "scope" && viz != "mirror") viz = "vu";
        theme = constrain((int)(d["theme"] | (int)theme), 0, 11);
        layout = constrain((int)(d["layout"] | (int)layout), 0, 11);
        bg_kind = constrain((int)(d["bg_kind"] | (int)bg_kind), 0, 4);
        ring_mode = constrain((int)(d["ring_mode"] | (int)ring_mode), 0, 3);
        night_theme = constrain((int)(d["night_theme"] | (int)night_theme), -1, 11);
    }
    JsonObjectConst in = o["input"];
    if (!in.isNull()) {
        encFromJson(in["enc1"], enc1);
        encFromJson(in["enc2"], enc2);
        ir = in["ir"] | ir;
        JsonObjectConst codes = in["ir_codes"];
        if (!codes.isNull()) {
            for (int i = 1; i < ACT_COUNT; i++) {
                const char* h = codes[ACTION_IDS[i]] | (const char*)nullptr;
                ir_codes[i] = h ? strtoul(h, nullptr, 0) : 0;
            }
        }
        JsonArrayConst bt = in["buttons"];
        if (!bt.isNull()) {
            for (int i = 0; i < MAX_BUTTONS; i++) {
                JsonObjectConst b = bt[i];
                if (b.isNull()) { btn[i] = BtnCfg(); continue; }
                btn[i].pin = b["pin"] | (int)-1;
                btn[i].action = actionFromId(b["action"] | "none");
            }
        }
    }
    if (disp != "none" && disp != "ssd1306" && disp != "sh1106" && disp != "ssd1309" && disp != "ili9341") disp = "none";
}

// ---------------------------------------------------------------------------
// Pini
// ---------------------------------------------------------------------------
PinClass pinClass(int g) {
    if (g < 0 || g > 48) return PIN_NA;
    if (g >= 22 && g <= 25) return PIN_NA;            // ne obstajajo
    if (g >= 26 && g <= 32) return PIN_BAD;           // SPI flash
    if (g >= 33 && g <= 37) return PIN_BAD;           // octal PSRAM (N16R8)
    if (g == 19 || g == 20) return PIN_BAD;           // USB D-/D+
    if (g == 0 || g == 3 || g == 45 || g == 46) return PIN_WARN;   // strapping
    if (g == 43 || g == 44) return PIN_WARN;          // UART0 (konzola)
    if (g == 38 || g == 48) return PIN_WARN;          // RGB LED na nekaterih ploščah
    return PIN_OK;
}

const char* pinNote(int g) {
    if (g >= 26 && g <= 32) return "SPI flash";
    if (g >= 33 && g <= 37) return "octal PSRAM";
    if (g == 19 || g == 20) return "USB";
    if (g == 0)  return "strapping (BOOT)";
    if (g == 3)  return "strapping (JTAG)";
    if (g == 45) return "strapping (VDD_SPI)";
    if (g == 46) return "strapping (ROM log)";
    if (g == 43) return "UART TX (konzola)";
    if (g == 44) return "UART RX (konzola)";
    if (g == 38 || g == 48) return "RGB LED (odvisno od plošče)";
    return "";
}

std::vector<PinUse> collectPins(const Config& c) {
    std::vector<PinUse> v;
    auto add = [&](int8_t g, const char* who) { if (g >= 0) v.push_back({g, who}); };
    add(c.i2s_bck, "Zvok BCK"); add(c.i2s_lck, "Zvok LCK"); add(c.i2s_din, "Zvok DIN");
    add(c.amp_en, "Vklop ojačevalnika");
    if (c.disp == "ili9341") {
        add(c.spi_sck, "Zaslon SCK"); add(c.spi_mosi, "Zaslon MOSI (SDI)"); add(c.spi_cs, "Zaslon CS");
        add(c.spi_dc, "Zaslon DC"); add(c.disp_rst, "Zaslon RESET"); add(c.bl, "Zaslon osvetlitev (LED)");
    } else if (c.disp != "none") {
        add(c.sda, "Zaslon SDA"); add(c.scl, "Zaslon SCL"); add(c.disp_rst, "Zaslon RST");
    }
    if (c.enc1.en) { add(c.enc1.a, "Enkoder 1 A"); add(c.enc1.b, "Enkoder 1 B"); add(c.enc1.sw, "Enkoder 1 tipka"); }
    if (c.enc2.en) { add(c.enc2.a, "Enkoder 2 A"); add(c.enc2.b, "Enkoder 2 B"); add(c.enc2.sw, "Enkoder 2 tipka"); }
    add(c.ir, "IR sprejemnik");
    static const char* bn[MAX_BUTTONS] = {"Tipka 1", "Tipka 2", "Tipka 3", "Tipka 4"};
    for (int i = 0; i < MAX_BUTTONS; i++) if (c.btn[i].action != ACT_NONE) add(c.btn[i].pin, bn[i]);
    return v;
}

bool validateConfig(const Config& c, std::vector<String>& err, std::vector<String>& warn) {
    auto pins = collectPins(c);
    for (size_t i = 0; i < pins.size(); i++) {
        const auto& p = pins[i];
        PinClass pc = pinClass(p.gpio);
        if (pc == PIN_NA)  err.push_back(p.owner + ": GPIO " + p.gpio + " ne obstaja");
        if (pc == PIN_BAD) err.push_back(p.owner + ": GPIO " + p.gpio + " je rezerviran (" + pinNote(p.gpio) + ")");
        if (pc == PIN_WARN) warn.push_back(p.owner + ": GPIO " + p.gpio + " – " + pinNote(p.gpio));
        for (size_t j = i + 1; j < pins.size(); j++)
            if (pins[j].gpio == p.gpio)
                err.push_back("GPIO " + String(p.gpio) + " je dodeljen dvakrat: " + p.owner + " in " + pins[j].owner);
    }
    if (c.i2s_bck < 0 || c.i2s_lck < 0 || c.i2s_din < 0) err.push_back("Zvok: BCK, LCK in DIN morajo biti nastavljeni");
    if (c.disp == "ili9341") {
        if (c.spi_sck < 0 || c.spi_mosi < 0 || c.spi_cs < 0 || c.spi_dc < 0) err.push_back("Zaslon: SCK, MOSI, CS in DC morajo biti nastavljeni");
    } else if (c.disp != "none" && (c.sda < 0 || c.scl < 0)) err.push_back("Zaslon: SDA in SCL morata biti nastavljena");
    auto encOk = [&](const EncCfg& e, const char* n) {
        if (e.en && (e.a < 0 || e.b < 0)) err.push_back(String(n) + ": pina A in B morata biti nastavljena");
    };
    encOk(c.enc1, "Enkoder 1"); encOk(c.enc2, "Enkoder 2");
    for (int i = 0; i < MAX_BUTTONS; i++)
        if (c.btn[i].action != ACT_NONE && c.btn[i].pin < 0)
            err.push_back("Tipka " + String(i + 1) + ": akcija je izbrana, pin pa ne");
    // 46 je samo vhod-izhod z omejitvami (ob zagonu mora biti LOW) - ne za pull-up vhode
    return err.empty();
}

bool needsReboot(const Config& a, const Config& b) {
    auto pa = collectPins(a), pb = collectPins(b);
    if (pa.size() != pb.size()) return true;
    for (size_t i = 0; i < pa.size(); i++)
        if (pa[i].gpio != pb[i].gpio || pa[i].owner != pb[i].owner) return true;
    return a.disp != b.disp || a.spi_mhz != b.spi_mhz || a.i2c_addr != b.i2c_addr || a.hostname != b.hostname ||
           a.ssid != b.ssid || a.pass != b.pass || a.enc1.en != b.enc1.en || a.enc2.en != b.enc2.en;
}

// ---------------------------------------------------------------------------
// Datoteka
// ---------------------------------------------------------------------------
static const char* CFG_PATH = "/config.json";

bool loadConfig(Config& c) {
    File f = LittleFS.open(CFG_PATH, "r");
    if (!f) return false;
    JsonDocument doc;
    DeserializationError e = deserializeJson(doc, f);
    f.close();
    if (e) { log_e("config.json: %s", e.c_str()); return false; }
    c.fromJson(doc.as<JsonObjectConst>());
    return true;
}

bool saveConfig(const Config& c) {
    FlashGuard g("nastavitve");
    JsonDocument doc;
    c.toJson(doc.to<JsonObject>(), true);
    File f = LittleFS.open("/config.tmp", "w");
    if (!f) return false;
    size_t n = serializeJson(doc, f);
    f.close();
    if (n == 0) return false;
    LittleFS.remove(CFG_PATH);
    return LittleFS.rename("/config.tmp", CFG_PATH);   // atomarno: izpad med pisanjem ne pokvari nastavitev
}
