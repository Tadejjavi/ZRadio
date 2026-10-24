#pragma once
#include <Arduino.h>

// Predvajalnik teče v lastnem FreeRTOS opravilu, ki je edini lastnik objekta Audio.
// Vsi ukazi gredo prek vrste (thread-safe), stanje se bere prek playerStatus().

struct PlayerStatus {
    bool     playing = false;     // tok teče
    bool     wanted  = false;     // uporabnik želi predvajanje
    bool     connecting = false;
    String   url;
    String   title;               // ICY StreamTitle
    String   icyName;
    String   codec;
    uint32_t bitrate = 0;         // bit/s
    uint32_t sampleRate = 0;
    uint8_t  bufferPct = 0;
    uint16_t bufferDs = 0;        // zvoka v medpomnilniku, desetinke sekunde (ocena iz bitne hitrosti)
    uint16_t vu = 0;              // L<<8 | R (0..127)
    uint32_t reconnects = 0;
    String   lastError;
};

bool playerBegin(int bck, int lck, int din);   // ustvari opravilo
void playerPlay(const String& url);
void playerStop();
void playerVolume(uint8_t vol);                // 0..VOL_STEPS
void playerMute(bool m);
void playerTone(int8_t low, int8_t mid, int8_t high);
void playerMono(bool m);
PlayerStatus playerStatus();
uint32_t playerTitleSeq();                     // poveča se ob vsaki spremembi naslova
uint32_t playerDecoderStackFree();             // rezerva sklada opravila knjižnice (dekoder), bajti
