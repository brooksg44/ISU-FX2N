#include "plc_scan.h"

#include "board.h"
#include "pico/stdlib.h"
#include "plc_memory.h"

static plc_mode_t mode = PLC_MODE_STOP;
static bool first_scan = false;
static uint64_t scan_start_us = 0;
static uint64_t last_tick_us = 0;

/* Fractional remainders, so a scan shorter than a tick is not lost. */
static uint32_t accum_us = 0;
static uint32_t ticks_1ms = 0, ticks_10ms = 0, ticks_100ms = 0;

/* Free-running millisecond count used to derive the clock pulse relays. */
static uint32_t clock_ms = 0;

static uint16_t timer_base_ms(uint16_t idx) {
    if (idx < 200) return 100;
    if (idx < 246) return 10;
    if (idx < 250) return 1;
    return 100;
}

static bool timer_is_retentive(uint16_t idx) { return idx >= 246; }

static void advance_ticks(void) {
    uint64_t now = time_us_64();
    uint32_t delta = (uint32_t)(now - last_tick_us);
    last_tick_us = now;

    accum_us += delta;
    uint32_t ms = accum_us / 1000u;
    accum_us %= 1000u;
    if (ms == 0) {
        ticks_1ms = ticks_10ms = ticks_100ms = 0;
        return;
    }

    clock_ms += ms;

    /* Carry counters so 10 ms and 100 ms bases stay exact over time. */
    static uint32_t rem_10 = 0, rem_100 = 0;
    ticks_1ms = ms;
    rem_10 += ms;
    ticks_10ms = rem_10 / 10u;
    rem_10 %= 10u;
    rem_100 += ms;
    ticks_100ms = rem_100 / 100u;
    rem_100 %= 100u;
}

static uint32_t ticks_for_base(uint16_t base_ms) {
    switch (base_ms) {
        case 1: return ticks_1ms;
        case 10: return ticks_10ms;
        default: return ticks_100ms;
    }
}

static void update_special_relays(void) {
    plc_set_m(M_RUN_MONITOR, mode == PLC_MODE_RUN);
    plc_set_m(M_INITIAL_PULSE, first_scan);

    /* Clock pulses are symmetric square waves, so each is on for half its
     * period. */
    plc_set_m(M_CLOCK_10MS, (clock_ms % 10u) < 5u);
    plc_set_m(M_CLOCK_100MS, (clock_ms % 100u) < 50u);
    plc_set_m(M_CLOCK_1S, (clock_ms % 1000u) < 500u);
    plc_set_m(M_CLOCK_1MIN, (clock_ms % 60000u) < 30000u);
}

void plc_scan_init(void) {
    plc_memory_init();

    /* Identify as an FX2N. Both D8001 and D8101 must carry the model code -
     * see the note in plc_memory.h for what GX Works reports otherwise. */
    plc_set_d(D_PLC_TYPE, FX2N_TYPE_VERSION);
    plc_set_d(D_MODEL_CODE, FX2N_MODEL_CODE);
    plc_set_d(D_WATCHDOG, FX2N_WATCHDOG_MS);
    plc_set_d(D_MEMORY_CAPACITY, FX2N_MEMORY_8K);
    plc_set_d(D_MEMORY_CAPACITY2, FX2N_MEMORY_8K);
    plc_set_d(D_MEMORY_TYPE, FX2N_MEMORY_TYPE_RAM);

    mode = PLC_MODE_STOP;
    first_scan = false;
    last_tick_us = time_us_64();
    accum_us = 0;
    clock_ms = 0;
}

plc_mode_t plc_scan_get_mode(void) { return mode; }

void plc_scan_set_mode(plc_mode_t new_mode) {
    if (new_mode == mode) {
        return;
    }
    mode = new_mode;
    /* Entering RUN arms the first-scan pulse; leaving it clears the outputs
     * so nothing is left energised in STOP. */
    first_scan = (mode == PLC_MODE_RUN);
    if (mode == PLC_MODE_STOP) {
        board_write_outputs(0);
    }
}

bool plc_scan_is_first(void) { return first_scan; }

void plc_scan_begin(void) {
    scan_start_us = time_us_64();
    advance_ticks();

    uint16_t inputs = board_read_inputs();
    for (uint16_t i = 0; i < BOARD_NUM_INPUTS; i++) {
        plc_set_x(i, (inputs >> i) & 1u);
    }

    for (uint8_t ch = 0; ch < BOARD_NUM_ANALOG; ch++) {
        plc_set_d((uint16_t)(D_ANALOG_BASE + ch), board_read_analog(ch));
    }

    update_special_relays();
}

void plc_scan_end(void) {
    if (mode == PLC_MODE_RUN) {
        uint16_t bits = 0;
        for (uint16_t i = 0; i < BOARD_NUM_OUTPUTS; i++) {
            if (plc_get_y(i)) {
                bits |= (uint16_t)(1u << i);
            }
        }
        board_write_outputs(bits);
    } else {
        board_write_outputs(0);
    }

    /* Scan time is reported in 0.1 ms units, as on a real FX. */
    uint32_t elapsed_us = (uint32_t)(time_us_64() - scan_start_us);
    uint16_t tenths = (uint16_t)(elapsed_us / 100u);
    plc_set_d(D_SCAN_TIME_CURRENT, tenths);
    if (tenths > plc_get_d(D_SCAN_TIME_MAX)) {
        plc_set_d(D_SCAN_TIME_MAX, tenths);
    }
    if (first_scan || tenths < plc_get_d(D_SCAN_TIME_MIN)) {
        plc_set_d(D_SCAN_TIME_MIN, tenths);
    }

    first_scan = false;
}

void plc_timer_drive(uint16_t idx, uint16_t preset, bool enable) {
    if (idx >= PLC_NUM_T) {
        return;
    }
    plc_timer_t *t = &plc_mem.t[idx];
    t->preset = preset;
    t->driven = enable;

    if (!enable) {
        if (!timer_is_retentive(idx)) {
            t->current = 0;
            t->done = false;
        }
        return;
    }

    uint32_t ticks = ticks_for_base(timer_base_ms(idx));
    if (ticks) {
        uint32_t next = (uint32_t)t->current + ticks;
        t->current = next > 0xFFFFu ? 0xFFFFu : (uint16_t)next;
    }
    if (preset != 0 && t->current >= preset) {
        t->current = preset;
        t->done = true;
    }
}

void plc_timer_reset(uint16_t idx) {
    if (idx >= PLC_NUM_T) {
        return;
    }
    plc_mem.t[idx].current = 0;
    plc_mem.t[idx].done = false;
    plc_mem.t[idx].driven = false;
}

void plc_counter_drive(uint16_t idx, int32_t preset, bool enable) {
    if (idx >= PLC_NUM_C) {
        return;
    }
    plc_counter_t *c = &plc_mem.c[idx];
    c->preset = preset;

    bool rising = enable && !c->last_in;
    c->last_in = enable;
    if (!rising) {
        return;
    }

    if (idx < 200) {
        /* 16-bit up counter: stops at preset. */
        if (c->current < preset) {
            c->current++;
        }
        if (c->current >= preset) {
            c->done = true;
        }
    } else {
        /* 32-bit bidirectional: M8200+idx selects down-counting, and the
         * contact reflects current >= preset rather than latching. */
        bool count_down = plc_get_m((uint16_t)(8200 + idx - 200));
        c->current += count_down ? -1 : 1;
        c->done = c->current >= preset;
    }
}

void plc_counter_reset(uint16_t idx) {
    if (idx >= PLC_NUM_C) {
        return;
    }
    plc_mem.c[idx].current = 0;
    plc_mem.c[idx].done = false;
    plc_mem.c[idx].last_in = false;
}
