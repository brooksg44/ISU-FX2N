/*
 * Host-side tests for the FX protocol address mapping.
 *
 * Build and run:  cc -I../src test_fx_addr.c ../src/fx_addr.c \
 *                    ../src/plc_memory.c -o test_fx_addr && ./test_fx_addr
 *
 * These cover the layer where a bug is invisible from the outside: the PLC
 * would answer GX Works 2 perfectly while reporting the wrong device.
 */
#include <stdio.h>
#include <string.h>

#include "fx_addr.h"
#include "plc_memory.h"

static int failures = 0;
static int checks = 0;

static void check(const char *what, long expected, long actual) {
    checks++;
    if (expected != actual) {
        failures++;
        printf("  FAIL %-44s expected %ld, got %ld\n", what, expected, actual);
    }
}

static void test_bit_devices(void) {
    plc_memory_init();

    /* X0 is bit 0 of the byte at 0x0080. */
    plc_set_x(0, true);
    check("X0 -> 0x0080 bit0", 0x01, fx_addr_read_byte(FX_ADDR_X));

    /* X7 is bit 7 of the same byte; X10 (octal) is bit 0 of the next one -
     * this is the octal-to-linear step that trips people up. */
    plc_memory_init();
    plc_set_x(7, true);
    check("X7 -> 0x0080 bit7", 0x80, fx_addr_read_byte(FX_ADDR_X));
    plc_memory_init();
    plc_set_x(8, true); /* X10 octal */
    check("X10(octal) -> 0x0081 bit0", 0x01, fx_addr_read_byte(FX_ADDR_X + 1));
    check("X10 does not appear at 0x0080", 0x00, fx_addr_read_byte(FX_ADDR_X));

    plc_memory_init();
    plc_set_y(0, true);
    plc_set_y(6, true);
    check("Y0+Y6 -> 0x00A0", 0x41, fx_addr_read_byte(FX_ADDR_Y));

    plc_memory_init();
    plc_set_m(0, true);
    check("M0 -> 0x0100 bit0", 0x01, fx_addr_read_byte(FX_ADDR_M));
    plc_memory_init();
    plc_set_m(1023, true);
    check("M1023 -> 0x017F bit7", 0x80, fx_addr_read_byte(FX_ADDR_M + 127));

    /* M8000 lives in its own region, not contiguous with M0. */
    plc_memory_init();
    plc_set_m(8000, true);
    check("M8000 -> 0x01E0 bit0", 0x01, fx_addr_read_byte(FX_ADDR_M_SPECIAL));

    plc_memory_init();
    plc_set_s(0, true);
    plc_set_s(9, true);
    check("S0 -> 0x0000 bit0", 0x01, fx_addr_read_byte(FX_ADDR_S));
    check("S9 -> 0x0001 bit1", 0x02, fx_addr_read_byte(FX_ADDR_S + 1));
}

static void test_word_devices(void) {
    plc_memory_init();

    /* D registers are little-endian across two byte addresses. */
    plc_set_d(0, 0x1234);
    check("D0 low byte  @0x1000", 0x34, fx_addr_read_byte(FX_ADDR_D));
    check("D0 high byte @0x1001", 0x12, fx_addr_read_byte(FX_ADDR_D + 1));

    /* The documented worked example: D123 sits at 0x10F6. */
    plc_set_d(123, 0xABCD);
    check("D123 low  @0x10F6", 0xCD, fx_addr_read_byte(0x10F6));
    check("D123 high @0x10F7", 0xAB, fx_addr_read_byte(0x10F7));

    /* D8000 is a separate region below D0, not an extension of it. */
    plc_set_d(8000, 0x5678);
    check("D8000 low  @0x0E00", 0x78, fx_addr_read_byte(FX_ADDR_D_SPECIAL));
    check("D8000 high @0x0E01", 0x56, fx_addr_read_byte(FX_ADDR_D_SPECIAL + 1));
}

static void test_timers_counters(void) {
    plc_memory_init();

    plc_mem.t[0].current = 0x0102;
    plc_mem.t[0].done = true;
    check("T0 current low  @0x0800", 0x02, fx_addr_read_byte(FX_ADDR_T_CURRENT));
    check("T0 current high @0x0801", 0x01, fx_addr_read_byte(FX_ADDR_T_CURRENT + 1));
    check("T0 contact -> 0x00C0 bit0", 0x01, fx_addr_read_byte(FX_ADDR_T_CONTACT));

    plc_mem.c[0].current = 0x4321;
    plc_mem.c[0].done = true;
    check("C0 current low  @0x0A00", 0x21, fx_addr_read_byte(FX_ADDR_C_CURRENT));
    check("C0 contact -> 0x01C0 bit0", 0x01, fx_addr_read_byte(FX_ADDR_C_CONTACT));

    /* C200+ are 32-bit and occupy four bytes each. */
    plc_memory_init();
    plc_mem.c[200].current = 0x11223344;
    check("C200 byte0 @0x0C00", 0x44, fx_addr_read_byte(FX_ADDR_C_CURRENT32));
    check("C200 byte3 @0x0C03", 0x11, fx_addr_read_byte(FX_ADDR_C_CURRENT32 + 3));
}

static void test_writes_round_trip(void) {
    plc_memory_init();

    fx_addr_write_byte(FX_ADDR_D, 0xEF);
    fx_addr_write_byte(FX_ADDR_D + 1, 0xBE);
    check("write D0 = 0xBEEF", 0xBEEF, plc_get_d(0));

    fx_addr_write_byte(FX_ADDR_M, 0x05); /* M0 and M2 */
    check("write M0", 1, plc_get_m(0));
    check("write M1 stays clear", 0, plc_get_m(1));
    check("write M2", 1, plc_get_m(2));

    fx_addr_write_bit(FX_ADDR_Y, 3, true);
    check("force Y3 on", 1, plc_get_y(3));
    fx_addr_write_bit(FX_ADDR_Y, 3, false);
    check("force Y3 off", 0, plc_get_y(3));
}

/* Force uses a flat device-number space, unlike read/write byte addresses. */
static void test_force_addresses(void) {
    plc_memory_init();

    fx_addr_force(3072, true); /* Y0, observed */
    check("force 3072 -> Y0 on", 1, plc_get_y(0));
    check("force 3072 does not touch M0", 0, plc_get_m(0));

    fx_addr_force(300, true); /* M300, observed */
    check("force 300 -> M300 on", 1, plc_get_m(300));

    fx_addr_force(3073, true);
    check("force 3073 -> Y1 on", 1, plc_get_y(1));

    /* X sits at 4608, recovered from the Monitor Mode watch lists. */
    fx_addr_force(4608, true);
    check("force 4608 -> X0 on", 1, plc_get_x(0));
    fx_addr_force(4615, true);
    check("force 4615 -> X7 on", 1, plc_get_x(7));

    fx_addr_force(0x1400 + 20, true);
    check("force 0x1414 -> S20 on", 1, plc_get_s(20));
    check("read 0x1414 -> S20", 1, fx_addr_force_read(0x1400 + 20));

    /* 0x0E00 is the special-relay block, which is where the error flags live. */
    fx_addr_force(3584 + 61, true);
    check("force 3645 -> M8061 on", 1, plc_get_m(8061));

    fx_addr_force(3072, false);
    check("force off 3072 -> Y0 off", 0, plc_get_y(0));

    /* Beyond the known region nothing may be written. */
    plc_memory_init();
    fx_addr_force(4000, true); /* between Y and the special block: unplaced */
    check("force 4000 ignored (M0)", 0, plc_get_m(0));
    check("force 4000 ignored (Y0)", 0, plc_get_y(0));
    check("force 4000 ignored (X0)", 0, plc_get_x(0));
    check("4000 reported unknown", 0, fx_addr_force_known(4000));
    check("3072 reported known", 1, fx_addr_force_known(3072));
    check("4608 reported known", 1, fx_addr_force_known(4608));
}

static void test_out_of_range_is_safe(void) {
    plc_memory_init();
    /* Unassigned gap and far-out addresses must not fault or corrupt. */
    check("gap 0x00E0 reads 0", 0x00, fx_addr_read_byte(0x00E8));
    check("gap 0x0400 reads 0", 0x00, fx_addr_read_byte(0x0400));
    fx_addr_write_byte(0x0400, 0xFF);
    fx_addr_write_byte(0xFFFF, 0xFF);
    check("still sane after stray writes", 0x00, fx_addr_read_byte(FX_ADDR_D));
}

int main(void) {
    test_bit_devices();
    test_word_devices();
    test_timers_counters();
    test_writes_round_trip();
    test_force_addresses();
    test_out_of_range_is_safe();

    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
