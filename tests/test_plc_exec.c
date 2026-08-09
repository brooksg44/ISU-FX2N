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
    if (idx >= PLC_NUM_C) return;
    plc_counter_t *c = &plc_mem.c[idx];
    c->preset = preset;
    bool rising = enable && !c->last_in;
    c->last_in = enable;
    if (!rising) return;
    if (c->current < preset) c->current++;
    if (c->current >= preset) c->done = true;
}
void plc_counter_reset(uint16_t idx) {
    if (idx >= PLC_NUM_C) return;
    plc_mem.c[idx].current = 0;
    plc_mem.c[idx].done = false;
    plc_mem.c[idx].last_in = false;
}

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

static void test_fsm_stl_initial_pulse_sets_s10(void) {
    /* Exact leading words captured from the FSM_STL download. The 0x0060
     * ZRST block is deliberately omitted here: this isolates the first-scan
     * contact and the extended SET-S form before the first STL step. */
    static const uint16_t program[] = {
        0x2F02, 0x0006, 0x800A, 0xF00A, 0xF7FF, 0x000F,
    };
    plc_memory_init();
    load_program(program, sizeof(program) / sizeof(program[0]));
    plc_set_m(M_INITIAL_PULSE, true);

    plc_exec_scan();

    check("captured LD M8002 / SET S10 initializes S10", 1, plc_get_s(10));
    check("captured first-scan prefix has no unknown opcode", 0,
          plc_exec_unknown_count());
}

static void test_fsm_pn_automatic_m_and_out_s(void) {
    /* Exact forms captured from FSM_PN. Nibble D extends M by 5*256, so FE
     * addresses M1534. The 0x0005/0x800A pair is OUT S10. */
    static const uint16_t program[] = {
        0x200A, 0x4400, 0x500B, 0xCDFE,
        0x2DFE, 0x0005, 0x800A, 0x000F,
    };
    plc_memory_init();
    load_program(program, sizeof(program) / sizeof(program[0]));
    plc_set_s(10, true);
    plc_set_x(0, true);

    plc_exec_scan();

    check("captured nibble-D coil writes M1534", 1, plc_get_m(1534));
    check("captured nibble-D contact reads M1534", 1, plc_get_s(10));
    check("captured FSM_PN forms have no unknown opcode", 0,
          plc_exec_unknown_count());
}

static void test_fsm_sr_transition_resets_source(void) {
    /* Captured FSM_SR_NO_SC S10 -> S11 transition. M1434 is the compiler's
     * automatic transition coil (nibble D, operand 0x9A). */
    static const uint16_t program[] = {
        0x200A, 0x4400, 0xCD9A,
        0x2D9A, 0x0006, 0x800B,
        0x2D9A, 0x0007, 0x800A,
        0x000F,
    };
    plc_memory_init();
    load_program(program, sizeof(program) / sizeof(program[0]));
    plc_set_s(10, true);
    plc_set_x(0, true);

    plc_exec_scan();

    check("FSM_SR transition sets destination S11", 1, plc_get_s(11));
    check("FSM_SR transition resets source S10", 0, plc_get_s(10));
    check("FSM_SR captured forms have no unknown opcode", 0,
          plc_exec_unknown_count());
}

static void test_captured_zrst_y_range(void) {
    static const uint16_t program[] = {
        0x2F00, 0x0060, 0x8400, 0x8005, 0x8403, 0x8005, 0x000F,
    };
    plc_memory_init();
    load_program(program, sizeof(program) / sizeof(program[0]));
    plc_set_m(M_RUN_MONITOR, true);
    for (uint16_t i = 0; i < 5; i++) plc_set_y(i, true);

    plc_exec_scan();

    check("captured ZRST clears Y0", 0, plc_get_y(0));
    check("captured ZRST clears Y3", 0, plc_get_y(3));
    check("captured ZRST leaves Y4 outside range", 1, plc_get_y(4));
    check("captured ZRST has no unknown opcode", 0, plc_exec_unknown_count());
}

static void test_fsm_sr_sc_first_transition_and_action(void) {
    /* Exact FSM_SR_SC forms for scan-control reset, initialization, S10->S11,
     * and the S10/S11 action rungs. */
    static const uint16_t program[] = {
        0x2F00, 0xEDFF,                   /* M8000 -> RST M1535 */
        0x2F02, 0x0006, 0x800A,           /* M8002 -> SET S10 */
        0x200A, 0x4400, 0x5DFF, 0xCD91,   /* transition -> M1425 */
        0x2D91, 0x0006, 0x800B,           /* SET S11 */
        0x2D91, 0x0007, 0x800A,           /* RST S10 */
        0x2D91, 0xDDFF,                   /* SET M1535 */
        0x200A, 0x0060, 0x8400, 0x8005, 0x8403, 0x8005,
        0x200B, 0xD500,                   /* S11 -> SET Y0 */
        0x001C,
    };
    plc_memory_init();
    load_program(program, sizeof(program) / sizeof(program[0]));
    plc_set_m(M_RUN_MONITOR, true);
    plc_set_m(M_INITIAL_PULSE, true);

    plc_exec_scan();
    check("FSM_SR_SC first scan initializes S10", 1, plc_get_s(10));
    check("FSM_SR_SC initial S10 action clears Y0", 0, plc_get_y(0));

    plc_set_m(M_INITIAL_PULSE, false);
    plc_set_x(0, true);
    plc_exec_scan();
    check("FSM_SR_SC transition resets S10", 0, plc_get_s(10));
    check("FSM_SR_SC transition sets S11", 1, plc_get_s(11));
    check("FSM_SR_SC scan-control bit is set", 1, plc_get_m(1535));
    check("FSM_SR_SC S11 action sets Y0 in transition scan", 1,
          plc_get_y(0));
    check("FSM_SR_SC captured forms have no unknown opcode", 0,
          plc_exec_unknown_count());
}

static void test_fsm_equ_comparison_and_moves(void) {
    /* Captured FSM_EQU core: initialize CurrentState (D100), compare
     * PreviousState (D101) with K1, then copy D100 to D101 at scan end. */
    static const uint16_t program[] = {
        0x2F02,
        0x0028, 0x8001, 0x8000, 0x86C8, 0x8600, /* MOV K1 D100 */
        0x01D0, 0x86CA, 0x8600, 0x8201, 0x8000, /* EQ D101 K1 */
        0xCD87,                                  /* OUT M1415 */
        0x2F00,
        0x0028, 0x86C8, 0x8600, 0x86CA, 0x8600, /* MOV D100 D101 */
        0x001C,
    };
    plc_memory_init();
    load_program(program, sizeof(program) / sizeof(program[0]));
    plc_set_m(M_RUN_MONITOR, true);
    plc_set_m(M_INITIAL_PULSE, true);

    plc_exec_scan();
    check("FSM_EQU initial MOVE writes D100", 1, plc_get_d(100));
    check("FSM_EQU comparison is false before previous-state copy", 0,
          plc_get_m(1415));
    check("FSM_EQU device MOVE copies D100 to D101", 1, plc_get_d(101));
    check("FSM_EQU captured forms have no unknown opcode", 0,
          plc_exec_unknown_count());

    plc_set_m(M_INITIAL_PULSE, false);
    plc_exec_scan();
    check("FSM_EQU comparison is true on following scan", 1,
          plc_get_m(1415));
    check("FSM_EQU following scan has no unknown opcode", 0,
          plc_exec_unknown_count());
}

static void test_fsm_shl_captured_sftl(void) {
    static const uint16_t program[] = {
        0x2F00,
        0x0056, 0x84F3, 0x800D, 0x840A, 0x8000,
        0x8009, 0x8000, 0x8001, 0x8000,
        0x001C,
    };
    plc_memory_init();
    load_program(program, sizeof(program) / sizeof(program[0]));
    plc_set_m(M_RUN_MONITOR, true);
    plc_set_s(10, true);

    plc_exec_scan();
    check("FSM_SHL shifts S10 into S11", 1, plc_get_s(11));
    check("FSM_SHL clears S10 from false source", 0, plc_get_s(10));

    plc_set_m(1523, true);
    plc_exec_scan();
    check("FSM_SHL loads source M1523 into S10", 1, plc_get_s(10));
    check("FSM_SHL advances prior state into S12", 1, plc_get_s(12));
    check("FSM_SHL captured SFTL has no unknown opcode", 0,
          plc_exec_unknown_count());
}

static void test_fsm_drum_absd_and_out_c(void) {
    static const uint16_t program[] = {
        0x2F00,
        0x0028, 0x8002, 0x8000, 0x8658, 0x8602,
        0x0028, 0x8009, 0x8000, 0x865A, 0x8602,
        0x0028, 0x8003, 0x8000, 0x865C, 0x8602,
        0x0028, 0x8008, 0x8000, 0x865E, 0x8602,
        0x0028, 0x8004, 0x8000, 0x8660, 0x8602,
        0x0028, 0x8007, 0x8000, 0x8662, 0x8602,
        0x0028, 0x8005, 0x8000, 0x8664, 0x8602,
        0x0028, 0x8006, 0x8000, 0x8666, 0x8602,
        0x2400, 0x6401,
        0x008C, 0x8658, 0x8602, 0x8600, 0x8400,
        0x8400, 0x8005, 0x8004, 0x8000,
        0x2E00, 0x000C, 0x8E00,
        0x01CA, 0x8400, 0x0E00, 0x8009, 0x8000,
        0x001C,
    };
    plc_memory_init();
    load_program(program, sizeof(program) / sizeof(program[0]));
    plc_set_m(M_RUN_MONITOR, true);
    plc_mem.c[0].current = 2;
    plc_set_x(0, true);

    plc_exec_scan();
    check("FSM_DRUM Initialize writes D300", 2, plc_get_d(300));
    check("FSM_DRUM Initialize writes D307", 6, plc_get_d(307));
    check("FSM_DRUM ABSD turns Y0 on at count 2", 1, plc_get_y(0));
    check("FSM_DRUM ABSD leaves Y1 off before count 3", 0, plc_get_y(1));
    check("FSM_DRUM OUT_C applies preset K9", 9,
          (unsigned)plc_mem.c[0].preset);
    check("FSM_DRUM OUT_C drives its input", 1,
          (unsigned)plc_mem.c[0].last_in);
    check("FSM_DRUM OUT_C increments C0", 3, (unsigned)plc_mem.c[0].current);
    check("FSM_DRUM captured forms have no unknown opcode", 0,
          plc_exec_unknown_count());

    plc_set_x(0, false);
    plc_exec_scan();
    plc_mem.c[0].current = 5;
    plc_mem.c[0].last_in = false;
    plc_set_x(0, true);
    plc_exec_scan();
    check("FSM_DRUM ABSD has all four outputs on at count 5", 0x0F,
          plc_get_y(0) | (plc_get_y(1) << 1) | (plc_get_y(2) << 2) |
              (plc_get_y(3) << 3));
}

static void test_fsm_counter_decode_captured_deco(void) {
    static const uint16_t program[] = {
        0x2F00,
        0x0062, 0x8600, 0x8400, 0x8409, 0x8000, 0x8004, 0x8000,
        0x001C,
    };
    plc_memory_init();
    load_program(program, sizeof(program) / sizeof(program[0]));
    plc_set_m(M_RUN_MONITOR, true);
    plc_mem.c[0].current = 3;

    plc_exec_scan();
    check("FSM_Counter_Decode C0=3 selects S12", 1, plc_get_s(12));
    check("FSM_Counter_Decode leaves adjacent S11 off", 0, plc_get_s(11));

    plc_mem.c[0].current = 7;
    plc_exec_scan();
    check("FSM_Counter_Decode clears previous S12", 0, plc_get_s(12));
    check("FSM_Counter_Decode C0=7 selects S16", 1, plc_get_s(16));
    check("FSM_Counter_Decode captured DECO has no unknown opcode", 0,
          plc_exec_unknown_count());
}

int main(void) {
    test_inactive_step_is_gated();
    test_transfer_and_handover();
    test_multiple_state_merge();
    test_ladder_set_is_not_a_transfer();
    test_fsm_stl_initial_pulse_sets_s10();
    test_fsm_pn_automatic_m_and_out_s();
    test_fsm_sr_transition_resets_source();
    test_captured_zrst_y_range();
    test_fsm_sr_sc_first_transition_and_action();
    test_fsm_equ_comparison_and_moves();
    test_fsm_shl_captured_sftl();
    test_fsm_drum_absd_and_out_c();
    test_fsm_counter_decode_captured_deco();
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
