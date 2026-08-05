/*
 * Host-side tests for the Modbus address mapping.
 *
 * cc -I../src test_modbus_map.c ../src/modbus_map.c ../src/plc_memory.c \
 *    -o test_modbus_map && ./test_modbus_map
 */
#include <stdio.h>

#include "modbus_map.h"
#include "plc_memory.h"

static int failures = 0, checks = 0;

static void check(const char *what, long expected, long actual) {
    checks++;
    if (expected != actual) {
        failures++;
        printf("  FAIL %-46s expected %ld, got %ld\n", what, expected, actual);
    }
}

static void test_coils(void) {
    plc_memory_init();
    bool v = false;

    plc_set_y(0, true);
    check("coil 0 reads Y0", 1, modbus_read_coil(0, &v) && v);

    plc_set_y(6, true);
    v = false;
    check("coil 6 reads Y6", 1, modbus_read_coil(6, &v) && v);

    modbus_write_coil(3, true);
    check("write coil 3 sets Y3", 1, plc_get_y(3));

    plc_set_m(300, true);
    v = false;
    check("coil 1300 reads M300", 1, modbus_read_coil(1300, &v) && v);

    modbus_write_coil(5020, true);
    check("write coil 5020 sets S20", 1, plc_get_s(20));

    /* Gaps between the blocks must be rejected, not silently read as 0. */
    check("coil 900 rejected (gap)", 0, modbus_read_coil(900, &v));
    check("coil 4500 rejected (gap)", 0, modbus_read_coil(4500, &v));
    check("coil 60000 rejected", 0, modbus_read_coil(60000, &v));
}

static void test_discrete_inputs(void) {
    plc_memory_init();
    bool v = false;

    plc_set_x(0, true);
    check("discrete 0 reads X0", 1, modbus_read_discrete(0, &v) && v);

    plc_mem.t[5].done = true;
    v = false;
    check("discrete 1005 reads T5", 1, modbus_read_discrete(1005, &v) && v);

    plc_mem.c[9].done = true;
    v = false;
    check("discrete 2009 reads C9", 1, modbus_read_discrete(2009, &v) && v);

    check("discrete 500 rejected (gap)", 0, modbus_read_discrete(500, &v));
}

static void test_registers(void) {
    plc_memory_init();
    uint16_t v = 0;

    plc_set_d(100, 1234);
    check("holding 100 reads D100", 1234, modbus_read_holding(100, &v) ? v : -1);

    modbus_write_holding(200, 4321);
    check("write holding 200 sets D200", 4321, plc_get_d(200));

    plc_set_d(8001, 24100);
    v = 0;
    check("holding 8001 reads D8001", 24100, modbus_read_holding(8001, &v) ? v : -1);

    plc_mem.t[3].current = 77;
    v = 0;
    check("input 3 reads T3 current", 77, modbus_read_input(3, &v) ? v : -1);

    plc_mem.c[4].current = 99;
    v = 0;
    check("input 1004 reads C4 current", 99, modbus_read_input(1004, &v) ? v : -1);

    plc_set_d(D_ANALOG_BASE + 1, 2048);
    v = 0;
    check("input 2001 reads AI1", 2048, modbus_read_input(2001, &v) ? v : -1);

    check("holding 9000 rejected", 0, modbus_read_holding(9000, &v));
    check("input 5000 rejected", 0, modbus_read_input(5000, &v));
}

int main(void) {
    test_coils();
    test_discrete_inputs();
    test_registers();
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
