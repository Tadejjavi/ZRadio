#include "input.h"
#include <soc/gpio_reg.h>
#include <esp_timer.h>

// ---------------------------------------------------------------------------
// Hiter branje pina v prekinitvi (brez klicev v flash)
// ---------------------------------------------------------------------------
static inline IRAM_ATTR int fastRead(int pin) {
    if (pin < 32) return (REG_READ(GPIO_IN_REG) >> pin) & 1;
    return (REG_READ(GPIO_IN1_REG) >> (pin - 32)) & 1;
}

// ---------------------------------------------------------------------------
// Enkoderji – kvadraturni dekoder s tabelo stanj (ISR na obeh pinih)
// ---------------------------------------------------------------------------
struct Enc {
    int8_t  a = -1, b = -1, sw = -1;
    uint8_t steps = 4;
    bool    inv = false;
    volatile uint8_t prev = 0;
    volatile int32_t raw = 0;
    int32_t used = 0;
    // tipka
    bool    swDown = false, longSent = false;
    uint32_t swT = 0, swChange = 0;
    bool    swStable = false;
};
static Enc enc[2];

// DRAM_ATTR: prekinitev GPIO je v Arduinu registrirana kot IRAM in teče tudi med pisanjem v flash
// (predpomnilnik izklopljen). Tabela v flashu (.rodata) bi takrat sprožila napako predpomnilnika.
static const DRAM_ATTR int8_t QT[16] = {0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0};

static void IRAM_ATTR encIsr(void* arg) {
    Enc* e = (Enc*)arg;
    uint8_t cur = (fastRead(e->a) << 1) | fastRead(e->b);
    uint8_t idx = (e->prev << 2) | cur;
    e->raw += QT[idx];
    e->prev = cur;
}

static void encSetup(Enc& e, const EncCfg& c) {
    if (!c.en || c.a < 0 || c.b < 0) return;
    e.a = c.a; e.b = c.b; e.sw = c.sw; e.steps = max<uint8_t>(1, c.steps); e.inv = c.invert;
    pinMode(e.a, INPUT_PULLUP);
    pinMode(e.b, INPUT_PULLUP);
    if (e.sw >= 0) pinMode(e.sw, INPUT_PULLUP);
    e.prev = (digitalRead(e.a) << 1) | digitalRead(e.b);
    attachInterruptArg(e.a, encIsr, &e, CHANGE);
    attachInterruptArg(e.b, encIsr, &e, CHANGE);
}

// ---------------------------------------------------------------------------
// IR – NEC dekoder na padajočih robovih (VS1838B: mirovanje = HIGH)
// ---------------------------------------------------------------------------
static int8_t irPin = -1;
static volatile int64_t  irLastEdge = 0;
static volatile uint32_t irData = 0;
static volatile uint8_t  irBits = 0;
static volatile bool     irCollect = false;
static volatile uint32_t irCode = 0;        // zadnja dekodirana koda
static volatile uint32_t irCodeSeq = 0;     // nova koda
static volatile uint32_t irRptSeq = 0;      // ponovitveni okvir
static volatile int64_t  irLastFrame = 0;

static void IRAM_ATTR irIsr() {
    int64_t now = esp_timer_get_time();
    uint32_t dt = (uint32_t)(now - irLastEdge);
    irLastEdge = now;
    if (dt > 12500 && dt < 14500) {            // vodilni impulz 9 ms + 4,5 ms
        irCollect = true; irBits = 0; irData = 0;
        return;
    }
    if (dt > 10500 && dt < 12300) {            // ponovitev 9 ms + 2,25 ms
        irCollect = false;
        if (now - irLastFrame < 200000) { irLastFrame = now; irRptSeq = irRptSeq + 1; }
        return;
    }
    if (!irCollect) return;
    if (dt > 900 && dt < 1450)       { /* bit 0 */ }
    else if (dt > 1900 && dt < 2600) { irData |= (1UL << irBits); }
    else { irCollect = false; return; }
    irBits = irBits + 1;
    if (irBits >= 32) {
        irCollect = false;
        irCode = irData; irCodeSeq = irCodeSeq + 1; irLastFrame = now;
    }
}

uint32_t irLastCode() { return irCode; }
uint32_t irSeq()      { return irCodeSeq; }

// ---------------------------------------------------------------------------
// Navadne tipke (na GND, notranji pull-up)
// ---------------------------------------------------------------------------
struct Btn {
    int8_t pin = -1;
    Action act = ACT_NONE;
    bool   stable = false, last = false;
    uint32_t tChange = 0, tDown = 0, tRpt = 0;
};
static Btn btns[MAX_BUTTONS];

static const Config* C = nullptr;
static uint32_t seenCode = 0, seenRpt = 0;
static Action  lastIrAct = ACT_NONE;

void inputBegin(const Config& c) {
    C = &c;
    encSetup(enc[0], c.enc1);
    encSetup(enc[1], c.enc2);
    for (int i = 0; i < MAX_BUTTONS; i++) {
        if (c.btn[i].pin >= 0 && c.btn[i].action != ACT_NONE) {
            btns[i].pin = c.btn[i].pin; btns[i].act = c.btn[i].action;
            pinMode(btns[i].pin, INPUT_PULLUP);
        }
    }
    if (c.ir >= 0) {
        irPin = c.ir;
        pinMode(irPin, INPUT_PULLUP);
        attachInterrupt(irPin, irIsr, FALLING);
    }
}

static bool pollEncButton(Enc& e, uint8_t src, InputEvent& ev) {
    if (e.sw < 0) return false;
    uint32_t now = millis();
    bool down = digitalRead(e.sw) == LOW;
    if (down != e.swStable) {
        if (now - e.swChange < 25) return false;   // razkrivanje odboja
        e.swStable = down; e.swChange = now;
        if (down) { e.swT = now; e.longSent = false; }
        else if (!e.longSent && now - e.swT < 700) { ev = {IN_ENC_CLICK, src, 0, false}; return true; }
    }
    if (e.swStable && !e.longSent && now - e.swT >= 800) {
        e.longSent = true; ev = {IN_ENC_LONG, src, 0, false}; return true;
    }
    return false;
}

bool inputPoll(InputEvent& ev) {
    uint32_t now = millis();
    // enkoderji
    for (int i = 0; i < 2; i++) {
        Enc& e = enc[i];
        if (e.a < 0) continue;
        int32_t raw = e.raw;
        int32_t det = raw / (int32_t)e.steps - e.used;
        if (det != 0) {
            e.used += det;
            if (e.inv) det = -det;
            ev = {IN_ENC_TURN, (uint8_t)(i + 1), (int16_t)det, false};
            return true;
        }
        if (pollEncButton(e, i + 1, ev)) return true;
    }
    // tipke
    for (auto& b : btns) {
        if (b.pin < 0) continue;
        bool down = digitalRead(b.pin) == LOW;
        if (down != b.last) { b.last = down; b.tChange = now; }
        if (now - b.tChange < 25 || down == b.stable) {
            // ponavljanje pri držanju (glasnost)
            if (b.stable && (b.act == ACT_VOL_UP || b.act == ACT_VOL_DOWN) &&
                now - b.tDown > 400 && now - b.tRpt > 120) {
                b.tRpt = now; ev = {IN_BTN, 0, (int16_t)b.act, true}; return true;
            }
            continue;
        }
        b.stable = down;
        if (down) { b.tDown = now; b.tRpt = now; ev = {IN_BTN, 0, (int16_t)b.act, false}; return true; }
    }
    // IR
    if (irPin >= 0) {
        if (irCodeSeq != seenCode) {
            seenCode = irCodeSeq; seenRpt = irRptSeq;
            uint32_t code = irCode;
            lastIrAct = ACT_NONE;
            for (int a = 1; a < ACT_COUNT; a++)
                if (C->ir_codes[a] && C->ir_codes[a] == code) { lastIrAct = (Action)a; break; }
            if (lastIrAct != ACT_NONE) { ev = {IN_IR, 0, (int16_t)lastIrAct, false}; return true; }
        } else if (irRptSeq != seenRpt) {
            seenRpt = irRptSeq;
            if (lastIrAct == ACT_VOL_UP || lastIrAct == ACT_VOL_DOWN) {
                ev = {IN_IR, 0, (int16_t)lastIrAct, true}; return true;
            }
        }
    }
    return false;
}
