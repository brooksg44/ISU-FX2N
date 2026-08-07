/*
 * ISU-FX2N - an open-source FX2N-compatible PLC for the Raspberry Pi Pico WH.
 *
 * Milestone 6: GX Works 2 can go online over USB CDC and monitor devices.
 *
 * The user program is still hardcoded in C below (demo_ladder) because the
 * instruction decoder does not exist yet - that is the next milestone, at
 * which point this function is replaced by the interpreter running a program
 * downloaded from GX Works 2. Until then the demo gives GX Works something
 * live to watch.
 *
 * Note there is no printf here. USB CDC carries the GX Works protocol, so any
 * stray debug output would corrupt a frame mid-flight. Debug output is only
 * compiled in when FX_TRACE is defined, and must not be enabled while a PLC
 * programming tool is connected.
 */
#include "board.h"
#include "fx_protocol.h"
#include "pico/stdlib.h"
#include "modbus.h"
#include "plc_exec.h"
#include "plc_memory.h"
#include "plc_program.h"
#include "plc_scan.h"
#include "plc_storage.h"

/* Stand-in for the downloaded user program:
 *   X0 starts, X1 stops    -> Y0   (seal-in)
 *   Y0 on for 2 s          -> Y1   (T0)
 *   five X0 presses        -> Y2   (C0, reset by X1)
 *   1 s clock pulse        -> Y6   (M8013)
 */
static void demo_ladder(void) {
    bool start = plc_get_x(0);
    bool stop = plc_get_x(1);

    bool y0 = (start || plc_get_y(0)) && !stop;
    plc_set_y(0, y0);

    plc_timer_drive(0, 20, y0); /* T0 is 100 ms base, so 20 = 2 s */
    plc_set_y(1, plc_mem.t[0].done);

    plc_counter_drive(0, 5, start);
    if (stop) {
        plc_counter_reset(0);
    }
    plc_set_y(2, plc_mem.c[0].done);

    plc_set_y(6, plc_get_m(M_CLOCK_1S));
}

/*
 * Local RUN/STOP on I9.
 *
 * A download issues a remote STOP ('B'), but the matching remote RUN command
 * has not been observed yet, so a downloaded program would otherwise stay
 * stopped until the next power cycle. I9 toggles the mode as a stand-in.
 *
 * This is close to what a real FX does anyway - it has a physical RUN/STOP
 * switch, which the trainer has no pin for - so this may well become the
 * permanent behaviour on a reserve pin rather than scaffolding.
 */
static void poll_run_stop_button(void) {
    static bool last = false;
    bool now = plc_get_x(9);
    if (now && !last) {
        plc_scan_set_mode(plc_scan_get_mode() == PLC_MODE_RUN ? PLC_MODE_STOP
                                                              : PLC_MODE_RUN);
    }
    last = now;
}

/*
 * sts1 (GPIO3) is the communications diagnostic. USB CDC carries the protocol
 * itself, so there is no terminal to print to - this LED is how the two
 * failure modes are told apart with no extra hardware:
 *
 *   dark              no protocol error
 *   0.5 s pulse       frame rejected - our parser or the address map
 *
 * Valid traffic is intentionally not displayed. Monitor Mode polls about ten
 * times per second, which otherwise makes this ERROR-labelled LED appear to
 * report a continuous fault while communication is healthy.
 */
static void update_comms_led(void) {
    static uint32_t last_bad = 0;
    static uint32_t bad_ms = 0;

    uint32_t now = to_ms_since_boot(get_absolute_time());

    uint32_t bad = fx_protocol_frames_bad();
    if (bad != last_bad) {
        last_bad = bad;
        bad_ms = now;
    }

    bool on = now - bad_ms < 500;
    board_status_led(1, on);
}

int main(void) {
    stdio_init_all();
    board_init();
    plc_program_init();
    plc_storage_load();
    plc_scan_init();
    fx_protocol_init();
    modbus_init(MODBUS_DEFAULT_SLAVE, MODBUS_DEFAULT_BAUD);

    /* No RUN/STOP switch exists on the trainer, so start in RUN. Remote
     * run/stop from GX Works 2 will take over this decision. */
    plc_scan_set_mode(PLC_MODE_RUN);

    while (true) {
        plc_scan_begin();
        if (plc_scan_get_mode() == PLC_MODE_RUN) {
            /* A downloaded program takes precedence; the built-in demo only
             * runs on a device that has never been programmed. */
            if (plc_exec_has_program()) {
                plc_exec_scan();
            } else {
                demo_ladder();
            }
        }
        plc_scan_end();

        /* Communications are serviced once per scan, exactly as on a real FX:
         * the PC sees a consistent snapshot rather than a half-updated one. */
        fx_protocol_task();
        modbus_task();

        board_status_led(0, plc_scan_get_mode() == PLC_MODE_RUN);
        update_comms_led();
        poll_run_stop_button();
        fx_protocol_idle_dump();
        plc_storage_task();
    }
}
