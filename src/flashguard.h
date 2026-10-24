#pragma once
#include <Arduino.h>

// Zaščita pisanja v flash.
// Med brisanjem/pisanjem flasha sta predpomnilnik in večina prekinitev izklopljena. Če čip briše
// sektor dlje od 300 ms (meja interrupt watchdoga), sledi INT_WDT. Zato med pisanjem začasno
// izklopimo INT_WDT, kot varovalo pa vklopimo RTC watchdog (8 s, neodvisen od prekinitev).
// Trajanje vsakega pisanja izmerimo; počasna pisanja se zapišejo v dnevnik.
void     flashGuardBegin();
void     flashGuardEnd(const char* what);
uint32_t flashGuardMaxMs();        // najdaljše pisanje od zagona
uint32_t flashGuardCount();

struct FlashGuard {
    const char* w;
    explicit FlashGuard(const char* what) : w(what) { flashGuardBegin(); }
    ~FlashGuard() { flashGuardEnd(w); }
};

