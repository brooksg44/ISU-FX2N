/*
 * plc_memory.h - the FX2N device model.
 *
 * Every PLC device (X, Y, M, S, T, C, D) lives here. Unlike the STM32 sources
 * this project is derived from - which packed everything into one flat
 * memory[] array addressed by hand-computed offsets - each device family gets
 * its own array. Programs are easier to read and a bad index cannot silently
 * corrupt an unrelated device family.
 *
 * FX2N device ranges implemented:
 *   X0-X377   (octal)  256 inputs          10 wired on the trainer
 *   Y0-Y377   (octal)  256 outputs          7 wired on the trainer
 *   M0-M3071           general relays
 *   M8000-M8255        special relays (run flags, clock pulses, ...)
 *   S0-S999            state relays (STL)
 *   T0-T255            timers
 *   C0-C255            counters
 *   D0-D7999           data registers
 *   D8000-D8255        special registers
 *
 * X and Y are numbered in OCTAL on a real FX PLC: X0-X7 then X10, X11. The
 * helpers below take a plain array index, so the octal-to-index conversion
 * belongs in the protocol and instruction-decoding layers, not here.
 */
#ifndef PLC_MEMORY_H
#define PLC_MEMORY_H

#include <stdbool.h>
#include <stdint.h>

#define PLC_NUM_X 256
#define PLC_NUM_Y 256
#define PLC_NUM_M 3072
#define PLC_NUM_M_SPECIAL 256 /* M8000..M8255 */
#define PLC_M_SPECIAL_BASE 8000
#define PLC_NUM_S 1000
#define PLC_NUM_T 256
#define PLC_NUM_C 256
#define PLC_NUM_D 8000
#define PLC_NUM_D_SPECIAL 256 /* D8000..D8255 */
#define PLC_D_SPECIAL_BASE 8000

/* Special relays this firmware maintains (FX2N standard assignments). */
#define M_RUN_MONITOR 8000    /* on while running */
#define M_INITIAL_PULSE 8002  /* on for the first scan only */
#define M_ALWAYS_ON 8000      /* alias for readability */
#define M_CLOCK_10MS 8011     /* 10 ms clock pulse */
#define M_CLOCK_100MS 8012    /* 100 ms clock pulse */
#define M_CLOCK_1S 8013       /* 1 s clock pulse */
#define M_CLOCK_1MIN 8014     /* 1 min clock pulse */

/* Special registers this firmware maintains. */
#define D_SCAN_TIME_CURRENT 8010 /* current scan time, 0.1 ms units */
#define D_SCAN_TIME_MIN 8011
#define D_SCAN_TIME_MAX 8012
#define D_ANALOG_BASE 8030 /* D8030..D8032 = AI0..AI2, raw 0-4095 */

/*
 * PLC identity, as GX Works 2 probes it. Determined experimentally against
 * GX Works 2 rather than taken from a manual, because the published type
 * codes are ambiguous: code 24 means FX2N in FX1S-era documentation but
 * FX3U/FX3UC in FX3-era documentation.
 *
 * The observed behaviour is that BOTH registers must carry the code:
 *
 *   D8001=0      D8101=0        -> "FX1CPU"        (falls back to oldest)
 *   D8001=24100  D8101=0        -> "FX3U/FX3UC"    (guesses newest)
 *   D8001=24100  D8101=24100    -> "FX2N/FX2NC"    (what we want)
 *
 * The value is <2-digit type code><3-digit version>, so 24100 is version 1.00.
 * Leaving D8101 at zero is what makes GX Works guess wrong - it is not an
 * FX3-only register as far as this identification path is concerned.
 */
#define D_WATCHDOG 8000
#define D_PLC_TYPE 8001
#define D_MEMORY_CAPACITY 8002
#define D_MEMORY_TYPE 8003
#define D_MODEL_CODE 8101
#define D_MEMORY_CAPACITY2 8102

/*
 * Memory cassette description, also determined experimentally.
 *
 *   D8002=8 alone          -> "Data exceeds capacity of the memory cassette"
 *   D8002 matching D8102   -> capacity check passes
 *   D8003=0x01             -> "cannot write during ROM operation" (read-only)
 *   D8003=0x00             -> RAM, writable
 *
 * D8102 mirroring D8002 is what actually satisfies the check; setting D8002
 * on its own is not enough.
 */
#define FX2N_MEMORY_TYPE_RAM 0x00

#define FX2N_TYPE_VERSION 24100
#define FX2N_MODEL_CODE 24100
#define FX2N_WATCHDOG_MS 200
#define FX2N_MEMORY_16K 16 /* 16000 program steps */

typedef struct {
    uint16_t current;  /* elapsed value */
    uint16_t preset;   /* set value the coil compares against */
    bool done;         /* the T contact */
    bool driven;       /* coil was energised this scan */
} plc_timer_t;

typedef struct {
    int32_t current; /* 32-bit so C200+ bidirectional counters fit */
    int32_t preset;
    bool done;    /* the C contact */
    bool last_in; /* previous coil state, for rising-edge counting */
} plc_counter_t;

typedef struct {
    uint16_t x[PLC_NUM_X / 16];
    uint16_t y[PLC_NUM_Y / 16];
    uint16_t m[PLC_NUM_M / 16];
    uint16_t m_special[PLC_NUM_M_SPECIAL / 16];
    uint16_t s[(PLC_NUM_S + 15) / 16];
    plc_timer_t t[PLC_NUM_T];
    plc_counter_t c[PLC_NUM_C];
    uint16_t d[PLC_NUM_D];
    uint16_t d_special[PLC_NUM_D_SPECIAL];
} plc_memory_t;

extern plc_memory_t plc_mem;

/* Clears every device. Retentive ranges are restored separately from flash. */
void plc_memory_init(void);

/*
 * Applies the current FX2N STOP -> RUN clear policy. Inputs, special relays,
 * special registers, retentive timers (T246-T255), and retentive counters
 * (C100-C255) are preserved. The emulator does not yet import configurable
 * M/S/D latch ranges from the PLC parameters, so those general device
 * families are presently treated as non-retentive.
 */
void plc_memory_reset_nonretentive(void);

/* Bit device access. Out-of-range indices read false and ignore writes, so a
 * malformed download cannot scribble over memory. */
bool plc_get_x(uint16_t i);
void plc_set_x(uint16_t i, bool v);
bool plc_get_y(uint16_t i);
void plc_set_y(uint16_t i, bool v);
bool plc_get_m(uint16_t i); /* accepts 0..3071 and 8000..8255 */
void plc_set_m(uint16_t i, bool v);
bool plc_get_s(uint16_t i);
void plc_set_s(uint16_t i, bool v);

/* Word device access. Accepts 0..7999 and 8000..8255. */
uint16_t plc_get_d(uint16_t i);
void plc_set_d(uint16_t i, uint16_t v);

/* 32-bit access to a D pair (D[i] low word, D[i+1] high word), as used by the
 * FX double-word instructions. */
uint32_t plc_get_d32(uint16_t i);
void plc_set_d32(uint16_t i, uint32_t v);

#endif /* PLC_MEMORY_H */
