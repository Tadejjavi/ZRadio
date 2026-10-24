#pragma once
#include <Arduino.h>

// Barvni TFT 320x240 (ILI9341, SPI) prek LovyanGFX.
// Slika se izriše v sprite v PSRAM, na zaslon pa se pošljejo le spremenjeni pasovi.
bool   tftBegin();
bool   tftActive();
void   tftUpdate(bool force, const char* toast);   // toast = nullptr, če ga ni
void   tftApplyConfig();
// predogled za panel: 160x120, RGB332 (1 B na piko) -> 19200 B
size_t tftSnapshot(uint8_t* out, size_t max, uint32_t& ageMs);
const char* tftThemeName(int i);
int    tftThemeCount();
void   tftBgChanged();              // naložena nova slika ozadja (/bg.565)
void   tftStats(float& fps, float& drawMs, float& pushMs);   // izmerjena hitrost osveževanja (drseče povprečje)
