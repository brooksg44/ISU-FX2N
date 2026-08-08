#include <stdio.h>

#include "plc_memory.h"

static int checks;
static int failures;

static void check(const char *what, long expected, long actual) {
    checks++;
    if (expected != actual) {
        failures++;
        printf("  FAIL %-52s expected %ld, got %ld\n", what, expected, actual);
    }
}

int main(void) {
    plc_memory_init();
    plc_set_x(0, true);
    plc_set_y(3, true);
    plc_set_m(42, true);
    plc_set_m(8002, true);
    plc_set_s(14, true);
    plc_set_d(300, 1234);
    plc_set_d(8001, 24100);
    plc_mem.t[0].current = 10;
    plc_mem.t[246].current = 20;
    plc_mem.c[0].current = 30;
    plc_mem.c[100].current = 40;

    plc_memory_reset_nonretentive();

    check("input image is preserved", 1, plc_get_x(0));
    check("Y image clears", 0, plc_get_y(3));
    check("general M clears", 0, plc_get_m(42));
    check("special M is preserved until scan maintenance", 1, plc_get_m(8002));
    check("S state clears", 0, plc_get_s(14));
    check("general D clears", 0, plc_get_d(300));
    check("special D identity is preserved", 24100, plc_get_d(8001));
    check("non-retentive timer clears", 0, plc_mem.t[0].current);
    check("retentive timer holds", 20, plc_mem.t[246].current);
    check("non-retentive counter clears", 0, plc_mem.c[0].current);
    check("retentive counter holds", 40, plc_mem.c[100].current);

    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
