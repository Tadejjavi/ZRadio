#pragma once
#include <Arduino.h>

// Logotipi postaj: 64x64 RGB565 (bajti kot v LGFX sprite), shranjeni v LittleFS /logo/<hash>.565
// ključ je URL toka postaje.
static constexpr int LOGO_W = 64, LOGO_BYTES = LOGO_W * LOGO_W * 2;
static constexpr int ICON_W = 24, ICON_BYTES = ICON_W * ICON_W * 2;

void        logoBegin();                                   // opravilo za prenos
String      logoPath(const String& stationUrl);
bool        logoSave(const String& stationUrl, const uint8_t* data, size_t len);
bool        logoDelete(const String& stationUrl);
bool        logoExists(const String& stationUrl);
// prenos in pretvorba slike (PNG, JPEG, ICO s PNG) v ozadju; vrne false, če je vrsta polna
bool        logoFetch(const String& stationUrl, const String& imageUrl);
void        logoStatus(int& pending, String& last);
// posrednik: radio prenese sliko (katerekoli oblike), brskalnik jo pretvori -> id naloge (0 = napaka)
uint32_t    logoGetRaw(const String& imageUrl);
uint32_t    logoRawDone();                       // id zadnje dokončane naloge
const uint8_t* logoRaw(size_t& len);             // nullptr, če prenos ni uspel

// za zaslon: kazalec na 64x64 ali 24x24 sliko v predpomnilniku (PSRAM) ali nullptr
const uint16_t* logoGet(const String& stationUrl);
const uint16_t* logoIcon(const String& stationUrl);
void        logoInvalidate(const String& stationUrl);
