#include "flashguard.h"
#include "diag.h"
#include <hal/wdt_hal.h>
#include <soc/timer_group_struct.h>

static portMUX_TYPE fgMux = portMUX_INITIALIZER_UNLOCKED;
static int      depth = 0;
static uint32_t t0 = 0, maxMs = 0, cnt = 0;

static constexpr uint32_t RWDT_TICKS = 136000UL * 8;   // RTC počasna ura ~136 kHz -> ~8 s

static void wdtPause() {
    wdt_hal_context_t rtc = RWDT_HAL_CONTEXT_DEFAULT();
    wdt_hal_write_protect_disable(&rtc);
    wdt_hal_config_stage(&rtc, WDT_STAGE0, RWDT_TICKS, WDT_STAGE_ACTION_RESET_SYSTEM);
    wdt_hal_feed(&rtc);
    wdt_hal_enable(&rtc);
    wdt_hal_write_protect_enable(&rtc);

    wdt_hal_context_t iw = {.inst = WDT_MWDT1, .mwdt_dev = &TIMERG1};
    wdt_hal_write_protect_disable(&iw);
    wdt_hal_disable(&iw);
    wdt_hal_write_protect_enable(&iw);
}

static void wdtResume() {
    wdt_hal_context_t iw = {.inst = WDT_MWDT1, .mwdt_dev = &TIMERG1};
    wdt_hal_write_protect_disable(&iw);
    wdt_hal_feed(&iw);
    wdt_hal_enable(&iw);
    wdt_hal_write_protect_enable(&iw);

    wdt_hal_context_t rtc = RWDT_HAL_CONTEXT_DEFAULT();
    wdt_hal_write_protect_disable(&rtc);
    wdt_hal_disable(&rtc);
    wdt_hal_write_protect_enable(&rtc);
}

void flashGuardBegin() {
    bool first;
    portENTER_CRITICAL(&fgMux);
    first = (depth++ == 0);
    if (first) { t0 = millis(); wdtPause(); }
    portEXIT_CRITICAL(&fgMux);
}

void flashGuardEnd(const char* what) {
    bool last; uint32_t ms = 0;
    portENTER_CRITICAL(&fgMux);
    last = (--depth == 0);
    if (last) { wdtResume(); ms = millis() - t0; cnt++; if (ms > maxMs) maxMs = ms; }
    portEXIT_CRITICAL(&fgMux);
    if (last && ms >= 200) diagEvent("Pisanje v flash (%s) je trajalo %lu ms", what, (unsigned long)ms);
}

uint32_t flashGuardMaxMs() { return maxMs; }
uint32_t flashGuardCount() { return cnt; }

