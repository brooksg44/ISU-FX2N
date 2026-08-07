/* Host-side tests for STL execution semantics. */
#include <stdio.h>
#include <string.h>

#include "plc_exec.h"
#include "plc_memory.h"
#include "plc_program.h"

#define W(opcode, operand) ((uint16_t)(((opcode) << 8) | (operand)))

#define OPC_STL 0xF0
#define OPC_RET 0xF7
#define OPC_END 0x00
#define OPC_LD_X 0x24
#define OPC_OR_X 0x64
#define OPC_OUT_Y 0xC5
#define OPC_SET_S 0xD0

static int failures;
static int checks;

/* plc_exec links against these scan services; STL tests do not use time. */
void plc_timer_drive(uint16_t idx, uint16_t preset, bool enable) {
    (void)idx; (void)preset; (void)enable;
}
void plc_timer_reset(uint16_t idx) { (void)idx; }
void plc_counter_drive(uint16_t idx, int32_t preset, bool enable) {
    (void)idx; (void)preset; (void)enable;
}
void plc_counter_reset(uint16_t idx) { (void)idx; }

static void check(const char *what, long expected, long actual) {
    checks++;
    if (expected != actual) {
        failures++;
        printf("  FAIL %-52s expected %ld, got %ld\n", what, expected, actual);
    }
}

static void load_program(const uint16_t *words, size_t count) {
    plc_program_init();
    for (size_t i = 0; i < count; i++) {
        uint32_t off = PLC_CODE_OFFSET + (uint32_t)i * 2;
        plc_program_write(off, (uint8_t)(words[i] & 0xFF));
        plc_program_write(off + 1, (uint8_t)(words[i] >> 8));
    }
}

static void test_inactive_step_is_gated(void) {
    static const uint16_t program[] = {
        W(OPC_STL, 20), W(OPC_LD_X, 0), W(OPC_OUT_Y, 0),
        W(OPC_LD_X, 1), W(OPC_OR_X, 0), W(OPC_OUT_Y, 1),
        W(OPC_RET, 0), W(OPC_END, 0x0F),
    };
    plc_memory_init();
    load_program(program, sizeof(program) / sizeof(program[0]));
    plc_set_x(0, true);
    plc_set_x(1, true);

    plc_exec_scan();

    check("inactive S20 gates LD rung", 0, plc_get_y(0));
    check("inactive S20 also gates OR rung", 0, plc_get_y(1));
}

static void test_transfer_and_handover(void) {
    static const uint16_t program[] = {
        W(OPC_STL, 20), W(OPC_OUT_Y, 0),
        W(OPC_LD_X, 0), W(OPC_SET_S, 21),
        W(OPC_STL, 21), W(OPC_OUT_Y, 1),
        W(OPC_RET, 0), W(OPC_END, 0x0F),
    };
    plc_memory_init();
    load_program(program, sizeof(program) / sizeof(program[0]));
    plc_set_s(20, true);
    plc_set_x(0, true);

    plc_exec_scan();
    check("transfer scan keeps source output active", 1, plc_get_y(0));
    check("later destination executes in transfer scan", 1, plc_get_y(1));
    check("source is reset at scan boundary", 0, plc_get_s(20));
    check("destination remains active", 1, plc_get_s(21));

    plc_exec_scan();
    check("source output clears on following scan", 0, plc_get_y(0));
    check("destination output remains active", 1, plc_get_y(1));
}

static void test_multiple_state_merge(void) {
    static const uint16_t program[] = {
        W(OPC_STL, 20), W(OPC_STL, 30),
        W(OPC_LD_X, 0), W(OPC_SET_S, 40),
        W(OPC_RET, 0), W(OPC_END, 0x0F),
    };
    plc_memory_init();
    load_program(program, sizeof(program) / sizeof(program[0]));
    plc_set_s(20, true);
    plc_set_x(0, true);

    plc_exec_scan();
    check("merge does not transfer with one source inactive", 0, plc_get_s(40));
    check("failed merge keeps active source", 1, plc_get_s(20));

    plc_set_s(30, true);
    plc_exec_scan();
    check("merge transfers when all sources are active", 1, plc_get_s(40));
    check("merge resets first source", 0, plc_get_s(20));
    check("merge resets second source", 0, plc_get_s(30));
}

static void test_ladder_set_is_not_a_transfer(void) {
    static const uint16_t program[] = {
        W(OPC_LD_X, 0), W(OPC_SET_S, 21), W(OPC_END, 0x0F),
    };
    plc_memory_init();
    load_program(program, sizeof(program) / sizeof(program[0]));
    plc_set_s(20, true);
    plc_set_x(0, true);

    plc_exec_scan();
    check("ordinary ladder SET activates state", 1, plc_get_s(21));
    check("ordinary ladder SET resets no source", 1, plc_get_s(20));
}

int main(void) {
    test_inactive_step_is_gated();
    test_transfer_and_handover();
    test_multiple_state_merge();
    test_ladder_set_is_not_a_transfer();
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
