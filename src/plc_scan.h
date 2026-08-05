/*
 * plc_scan.h - the PLC scan cycle.
 *
 * A PLC does not run like an ordinary program. It repeats a fixed cycle:
 *
 *   1. read every physical input into the X image        plc_scan_begin()
 *   2. execute the user program against that image       (caller)
 *   3. write the Y image out to the physical outputs     plc_scan_end()
 *
 * Because step 2 works on a snapshot, an input that changes mid-scan is not
 * seen until the next cycle - this is what makes ladder logic deterministic,
 * and it is the single most important idea for students to take from the
 * source.
 *
 * Timers and counters are driven from the user program (a real FX updates a
 * timer where its OUT instruction executes, not at end of scan), so the drive
 * functions below are what the instruction executor calls.
 */
#ifndef PLC_SCAN_H
#define PLC_SCAN_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    PLC_MODE_STOP = 0,
    PLC_MODE_RUN = 1,
} plc_mode_t;

void plc_scan_init(void);

plc_mode_t plc_scan_get_mode(void);
void plc_scan_set_mode(plc_mode_t mode);

/* Step 1: latch inputs, advance clock pulses, maintain special relays. */
void plc_scan_begin(void);

/* Step 3: publish outputs and record scan time in D8010-D8012. In STOP the
 * physical outputs are forced off, matching FX behaviour. */
void plc_scan_end(void);

/* True only during the first scan after entering RUN (drives M8002). */
bool plc_scan_is_first(void);

/*
 * Timer control, called by the OUT T instruction.
 * Time base follows the FX2N assignment:
 *   T0-T199    100 ms
 *   T200-T245   10 ms
 *   T246-T249    1 ms, retentive
 *   T250-T255  100 ms, retentive
 * A non-retentive timer resets when its coil drops. A retentive one holds its
 * accumulated value until plc_timer_reset().
 */
void plc_timer_drive(uint16_t idx, uint16_t preset, bool enable);
void plc_timer_reset(uint16_t idx);

/*
 * Counter control, called by the OUT C instruction. Counts one per rising
 * edge of enable. C0-C99 general and C100-C199 retentive are 16-bit up
 * counters; C200-C234 are 32-bit bidirectional (direction from M8200+idx).
 */
void plc_counter_drive(uint16_t idx, int32_t preset, bool enable);
void plc_counter_reset(uint16_t idx);

#endif /* PLC_SCAN_H */
