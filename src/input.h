#pragma once
#include <Arduino.h>
#include "config.h"

// Dogodki vhodov, ki jih obdela app
enum InEvt : uint8_t {
    IN_NONE = 0,
    IN_ENC_TURN,     // src: 1/2, val: +-koraki
    IN_ENC_CLICK,    // src: 1/2
    IN_ENC_LONG,     // src: 1/2
    IN_BTN,          // val: Action (kratek pritisk ali ponavljanje)
    IN_IR,           // val: Action, rpt: ponovitev
};

struct InputEvent {
    InEvt   type = IN_NONE;
    uint8_t src = 0;
    int16_t val = 0;
    bool    rpt = false;
};

void inputBegin(const Config& c);
bool inputPoll(InputEvent& e);          // kliči v loop(); vrne true, ko je dogodek
uint32_t irLastCode();                  // zadnja prejeta koda (tudi neznana) – za učenje
uint32_t irSeq();
uint32_t irBadCount();                  // zavrženi okvirji (ni NEC / motnje)                       // števec prejetih kod
