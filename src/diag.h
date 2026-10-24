#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

// Diagnostika: zgodovina zagonov (z razlogom in stanjem tik pred njim),
// povzetek sesutja (coredump), dnevnik dogodkov, ki preživi ponovni zagon,
// kontrolne točke zdravja pomnilnika in števci napak.

enum DiagCounter : uint8_t {
    DC_WIFI_DISC = 0,   // izgube WiFi povezave
    DC_STREAM_FAIL,     // neuspele povezave na tok
    DC_STREAM_STALL,    // zastoji toka (čuvaj)
    DC_OLED_REINIT,     // ponovne inicializacije zaslona
    DC_OLED_LOST,       // zaslon ni odgovarjal na I2C
    DC_COUNT
};

void diagBegin();                         // čim prej v setup(), po LittleFS.begin()
void diagSetSafe(bool safe, const char* why, uint8_t bootfails);
void diagLoop();                          // v loop()
void diagEvent(const char* fmt, ...);     // dogodek v dnevnik (tudi Serial)
void diagCount(DiagCounter c);
void diagSetAudioTask(TaskHandle_t h);
void diagToJson(JsonObject o);
String diagLogText();                     // dnevnik (prejšnji + trenutni) za prenos
void diagClear();
bool diagLastBootBad();                   // zadnji zagon je bil sesutje / watchdog / padec napetosti
