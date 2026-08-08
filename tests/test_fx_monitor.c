#include <stdio.h>

#include "fx_monitor.h"
#include "plc_memory.h"

static int checks;
static int failures;

static void check(const char *name, unsigned expected, unsigned actual) {
    checks++;
    if (expected != actual) {
        failures++;
        printf("  FAIL %-42s expected %u, got %u\n", name, expected, actual);
    }
}

static void write_list(const uint8_t *data, uint16_t len) {
    for (uint16_t i = 0; i < len; i++) {
        fx_monitor_write((uint16_t)(FX_MON_LIST + i), data[i]);
    }
}

/* Captured while GX Works displayed STL example 2 in ladder and watch views. */
static const uint8_t stl2_list[] = {
    0x03, 0x81, 0x0F, 0x00,
    0x18, 0x0E, 0x06, 0x0E, 0x14, 0x0E,
    0x00, 0x12, 0x02, 0x0C, 0x02, 0x12, 0x00, 0x0C,
    0x28, 0x14, 0x01, 0x0C, 0x00, 0x0E, 0x3D, 0x0E,
    0x1E, 0x14, 0x58, 0x0E, 0x40, 0x0E, 0x14, 0x14,
    0x41, 0x0E, 0x42, 0x0E, 0x01, 0x12,
};

int main(void) {
    plc_memory_init();
    write_list(stl2_list, sizeof stl2_list);

    plc_set_d(8012, 0x0014);
    plc_set_d(8003, 0xFFFF);
    plc_set_d(8010, 0x0014);
    plc_set_x(0, true);       /* bit entry 0 */
    plc_set_y(0, true);       /* bit entry 3 */
    plc_set_s(40, true);      /* bit entry 4 */
    plc_set_m(8000, true);    /* bit entry 6 */
    plc_set_s(20, true);      /* bit entry 11 */
    plc_set_x(1, true);       /* bit entry 14 */
    fx_monitor_sample();

    check("three words + fifteen bits -> 8 bytes", 8, fx_monitor_result_len());
    check("D8012 low", 0x14, fx_monitor_read(FX_MON_RESULT + 0));
    check("D8012 high", 0x00, fx_monitor_read(FX_MON_RESULT + 1));
    check("D8003 low", 0xFF, fx_monitor_read(FX_MON_RESULT + 2));
    check("D8003 high", 0xFF, fx_monitor_read(FX_MON_RESULT + 3));
    check("D8010 low", 0x14, fx_monitor_read(FX_MON_RESULT + 4));
    check("first eight watched bits", 0x59, fx_monitor_read(FX_MON_RESULT + 6));
    check("remaining watched bits", 0x48, fx_monitor_read(FX_MON_RESULT + 7));

    plc_set_x(0, false);
    fx_monitor_sample();
    check("X0 updates on next sample", 0x58, fx_monitor_read(FX_MON_RESULT + 6));

    plc_set_d(512, 0xBEEF);
    write_list(stl2_list, sizeof stl2_list);
    check("watch list does not overwrite D512", 0xBEEF, plc_get_d(512));

    /* Captured from FSM_EQU: GX Works encodes D100/D101 as byte addresses
     * 0x40C8/0x40CA in the monitor list. */
    static const uint8_t equ_words[] = {
        0x02, 0x81, 0x00, 0x00, 0xC8, 0x40, 0xCA, 0x40,
    };
    write_list(equ_words, sizeof equ_words);
    plc_set_d(100, 1);
    plc_set_d(101, 2);
    fx_monitor_sample();
    check("FSM_EQU D100 monitor low", 1, fx_monitor_read(FX_MON_RESULT));
    check("FSM_EQU D100 monitor high", 0, fx_monitor_read(FX_MON_RESULT + 1));
    check("FSM_EQU D101 monitor low", 2, fx_monitor_read(FX_MON_RESULT + 2));
    check("FSM_EQU D101 monitor high", 0, fx_monitor_read(FX_MON_RESULT + 3));

    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
