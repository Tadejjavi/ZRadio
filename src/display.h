#pragma once
#include <Arduino.h>

bool displayBegin();                 // glede na cfg.disp ustvari gonilnik (lahko "none")
void displayUpdate(bool force);      // kliči pogosto; sam omeji hitrost osveževanja
void displayToast(const char* msg);  // kratko sporočilo v nogi (2 s)
void displayApplyConfig();           // kontrast, rotacija
// predogled za panel: OLED "mono-128x64" (1024 B, strani po 8 vrstic) ali TFT "rgb332-160x120" (19200 B)
size_t displaySnapshot(uint8_t* out, size_t max, uint32_t& ageMs, const char*& fmt);
