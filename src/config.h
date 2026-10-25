#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>

// ---------------------------------------------------------------------------
// Akcije (enkoderji, tipke, IR daljinec)
// ---------------------------------------------------------------------------
enum Action : uint8_t {
    ACT_NONE = 0,
    ACT_VOL_UP,
    ACT_VOL_DOWN,
    ACT_NEXT,
    ACT_PREV,
    ACT_PLAY_STOP,
    ACT_MUTE,
    ACT_POWER,
    ACT_FAV_TOGGLE,
    ACT_BROWSE,
    ACT_OK,
    ACT_MENU,          // meni: odpri / potrdi
    ACT_CH_PREV,       // v meniju gor, sicer prejšnja postaja
    ACT_CH_NEXT,       // v meniju dol, sicer naslednja postaja
    ACT_EQ,            // naslednja prednastavitev izenačevalnika
    ACT_SLEEP,         // časovnik izklopa: 15/30/60/90/izklop
    ACT_D0, ACT_D1, ACT_D2, ACT_D3, ACT_D4, ACT_D5, ACT_D6, ACT_D7, ACT_D8, ACT_D9,   // številka postaje
    ACT_COUNT
};
extern const char* const ACTION_IDS[ACT_COUNT];   // "vol_up", ...
Action actionFromId(const char* id);

static constexpr int MAX_BUTTONS = 4;
static constexpr int VOL_STEPS = 30;               // glasnost 0..30

struct EncCfg {
    bool   en    = false;
    int8_t a     = -1;
    int8_t b     = -1;
    int8_t sw    = -1;
    uint8_t steps = 4;     // prehodov na klik (EC11: 4, nekateri 2)
    bool   invert = false;
};

struct BtnCfg {
    int8_t  pin    = -1;
    Action  action = ACT_NONE;
};

struct Config {
    // --- omrežje ---
    String hostname = "zradio";
    String ssid;
    String pass;
    String tz  = "CET-1CEST,M3.5.0,M10.5.0/3";   // Europe/Ljubljana
    String ntp = "pool.ntp.org";

    // --- zvok ---
    int8_t i2s_bck = 21, i2s_lck = 15, i2s_din = 47;
    int8_t amp_en = -1;          // izhod za vklop ojačevalnika (HIGH = vklopljen)
    bool   amp_inv = false;
    uint8_t vol_limit = VOL_STEPS;
    int8_t eq_low = 0, eq_mid = 0, eq_high = 0;   // -12..12 dB
    bool   mono = false;
    bool   autoplay = true;

    // --- zaslon ---
    String disp = "ssd1306";     // none | ssd1306 | sh1106 | ssd1309 | ili9341
    int8_t sda = 8, scl = 9, disp_rst = -1;
    uint8_t spi_mhz = 40;        // hitrost SPI za TFT (40 ali 80)
    int8_t spi_sck = 12, spi_mosi = 11, spi_cs = 10, spi_dc = 9, bl = 2;   // TFT (SPI) + osvetlitev (PWM)
    uint8_t i2c_addr = 0x3C;
    bool   rot180 = false;
    uint8_t contrast = 180;
    uint8_t contrast_night = 8;
    uint8_t night_from = 22, night_to = 7;        // ure (0..23), from==to -> izklop
    uint8_t saver_min = 10;      // 0 = brez ohranjevalnika zaslona
    uint8_t refresh_min = 10;    // periodična ponovna inicializacija OLED (0 = izklop)
    bool   standby_clock = true; // ura v stanju pripravljenosti
    String viz = "vu";          // vu | spectrum | mirror | analog | scope (barvni zaslon)
    uint8_t theme = 0;           // barvna tema (0..11)
    uint8_t layout = 0;          // postavitev glavnega zaslona: 0 klasična, 1 kartica, 2 skala, 3 velika ura, 4 celozaslonski
    int8_t  night_theme = -1;    // tema ponoči (-1 = ista kot podnevi)
    uint8_t bg_kind = 0;         // ozadje postavitve Obroči: 0 nebo, 1 morje, 2 zahod, 3 barve teme, 4 lastna slika
    uint8_t ring_mode = 0;       // levi obroč: 0 čas pesmi, 1 ura, 2 čas poslušanja, 3 medpomnilnik

    // --- vhodi ---
    EncCfg enc1, enc2;
    int8_t ir = -1;
    uint32_t ir_codes[ACT_COUNT] = {0};
    BtnCfg btn[MAX_BUTTONS];

    Config();
    void toJson(JsonObject o, bool withSecrets) const;
    void fromJson(JsonObjectConst o);    // manjkajoča polja ostanejo nespremenjena
};

// ---------------------------------------------------------------------------
// Pini ESP32-S3 (N16R8 / octal PSRAM)
// ---------------------------------------------------------------------------
enum PinClass : uint8_t { PIN_NA = 0, PIN_OK = 1, PIN_WARN = 2, PIN_BAD = 3 };
PinClass pinClass(int gpio);
const char* pinNote(int gpio);

struct PinUse { int8_t gpio; String owner; };
std::vector<PinUse> collectPins(const Config& c);
// Vrne true, če je konfiguracija veljavna. errors/warnings v slovenščini.
bool validateConfig(const Config& c, std::vector<String>& errors, std::vector<String>& warnings);
bool needsReboot(const Config& a, const Config& b);

bool loadConfig(Config& c);
bool saveConfig(const Config& c);

extern Config cfg;
