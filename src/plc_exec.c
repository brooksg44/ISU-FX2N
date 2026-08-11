#include "plc_exec.h"

#include "plc_memory.h"
#include "plc_program.h"
#include "plc_scan.h"

#include <string.h>

/* Device type nibbles. */
#define DEV_X 0x4
#define DEV_Y 0x5
#define DEV_T 0x6
#define DEV_M 0x8
#define DEV_C 0xE
#define DEV_MS 0xF /* special M, operand is the number minus 8000 */

/*
 * Device numbers above 255 do not fit the 8-bit operand. The low nibble
 * carries the high bits instead: OUT M300 encodes as opcode 0xC9 operand 0x2C,
 * i.e. nibble 9 = M + 256, 256 + 44 = 300. M spans nibbles 8..D and S spans
 * 0..3. FSM_PN confirms nibble D: 0xCDFE is OUT M1534.
 */
#define DEV_S 0x0 /* S0..S255, then 1/2/3 for +256/+512/+768 */

/* Operation nibbles. */
#define OP_MISC 0x0
#define OP_LD 0x2
#define OP_LDI 0x3
#define OP_AND 0x4
#define OP_ANI 0x5
#define OP_OR 0x6
#define OP_ORI 0x7 /* inferred from the pattern; not yet observed */
#define OP_PLS 0x8
#define OP_CONST 0x8 /* opcode 0x80 exactly: a constant word */
#define OP_OUT 0xC
#define OP_SET 0xD
#define OP_RST 0xE

/*
 * Members of the misc group (opcode 0x00), selected by the operand.
 * These are the instructions with no device of their own.
 */
#define MISC_OUT_S 0x05 /* followed by one value word holding the S number */
#define MISC_SET_S 0x06 /* followed by one value word holding the S number */
#define MISC_RST_S 0x07 /* followed by one value word holding the S number */
#define MISC_PLS_PREFIX 0x08
/*
 * PLF prefix, captured by downloading a single rung and reading program
 * memory back:
 *
 *   2401  LD X1
 *   0009  PLF prefix
 *   8801  PLF M1
 *
 * So PLF shares the PLS device word and differs only in the prefix. This also
 * confirms the ordering the surrounding misc operands suggest: 0x05 OUT S,
 * 0x06 SET S, 0x07 RST S, 0x08 PLS, 0x09 PLF.
 */
#define MISC_PLF_PREFIX 0x09
#define MISC_RST_C 0x0C /* Structured Ladder counter reset + counter word */
#define MISC_CALL 0x12 /* followed by a typed P pointer operand */
#define MISC_SRET 0x14
#define MISC_END 0x0F
#define MISC_FEND 0x1C /* program body terminator emitted before END */
#define MISC_MOV 0x28 /* followed by two typed operand pairs */
/*
 * DMOV - the 32-bit form of MOV. Applied instructions sit at 0x10 + 2 * FNC,
 * so MOV (FNC 12) is 0x28 and the odd neighbour is its double-word form. DIV
 * and DDIV below repeat the pattern at 0x3E/0x3F, which is what makes the odd
 * slot a rule rather than a coincidence.
 *
 * It was first captured as the 32-bit TIME move inside the IEC timer function
 * blocks; nothing about the encoding is TIME-specific. Each 32-bit operand is
 * two typed operand pairs, low word first.
 */
#define MISC_DMOV 0x29
#define MISC_CMP 0x24
#define MISC_BMOV 0x2E
#define MISC_FMOV 0x30
#define MISC_ADD 0x38
#define MISC_SUB 0x3A
#define MISC_MUL 0x3C
#define MISC_DIV 0x3E
#define MISC_DDIV_TIME 0x3F /* captured 32-bit TIME scaling used by IEC timers */
#define MISC_INC 0x40
#define MISC_DEC 0x42
#define MISC_WAND 0x44
#define MISC_WOR 0x46
#define MISC_WXOR 0x48
#define MISC_NEG 0x4A
#define MISC_ROR 0x4C
#define MISC_ROL 0x4E
#define MISC_SFTR 0x54
#define MISC_SFWR 0x5C
#define MISC_SFRD 0x5E
#define MISC_SFTL 0x56 /* source bit, destination bit, length, shift count */
#define MISC_ZRST 0x60 /* followed by two typed bit-device operand pairs */
#define MISC_DECO 0x62 /* source value, first destination bit, source width */
#define MISC_ENCO 0x64
#define MISC_BON 0x68
#define MISC_FLT 0x72
#define MISC_ABSD 0x8C /* table, counter, first output, output count */

/* Stack and block operations: opcode 0xFF, operand selects. */
#define OPCODE_STACK 0xFF
#define STACK_ANB 0xF8
#define STACK_ORB 0xF9 /* inferred from ANB; not yet observed */
#define STACK_MPS 0xFA
#define STACK_MRD 0xFB
#define STACK_MPP 0xFC

#define OPCODE_STL 0xF0 /* operand is the S number */
#define OPCODE_RET 0xF7

/* Typed operands of applied instructions, two words each, value carried in
 * the operand bytes low word first. */
#define OPERAND_CONST 0x80
#define OPERAND_COMPARE_CONST 0x82
#define OPERAND_BIT 0x84
#define OPERAND_D 0x86
#define OPERAND_POINTER 0x88

/* Structured Ladder EQ_E-2 prefix captured as bytes D0 01 in FSM_EQU. The
 * program image is fetched little-endian, hence instruction word 0x01D0. */
#define EQ_E_WORD 0x01D0
#define OUT_C_ENABLE_WORD 0x01CA

#define INLINE_LD_EQ 0x01D0
#define INLINE_LD_GT 0x01D2
#define INLINE_LD_LT 0x01D4
#define INLINE_LD_NE 0x01D8
#define INLINE_LD_LE 0x01DA
#define INLINE_LD_GE 0x01DC
#define INLINE_AND_EQ 0x01E0
#define INLINE_AND_GT 0x01E2
#define INLINE_AND_LT 0x01E4
#define INLINE_AND_NE 0x01E8
#define INLINE_AND_LE 0x01EA
#define INLINE_AND_GE 0x01EC
#define INLINE_OR_EQ 0x01F0
#define INLINE_OR_GT 0x01F2
#define INLINE_OR_LT 0x01F4
#define INLINE_OR_NE 0x01F8
#define INLINE_OR_LE 0x01FA
#define INLINE_OR_GE 0x01FC

#define OPCODE_SUBROUTINE 0xB0
#define CALL_STACK_DEPTH 8

/* Preset coils live in the misc group with a device nibble. */
#define OPCODE_OUT_T 0x06
#define OPCODE_OUT_C 0x0E
#define OPCODE_CONST 0x80

#define WORD_BLANK 0xFFFF

#define STL_MAX_SOURCES 8
#define STL_RESET_WORDS ((PLC_NUM_S + 31) / 32)

/*
 * Edge memory for the pulse instructions, one bit per program step.
 *
 * PLS must fire for exactly one scan on a rising rung, so the edge has to be
 * measured against what the rung did at THIS instruction on the previous scan.
 * Deriving it from the destination device instead does not work: with the rung
 * held true the device itself alternates, so the coil toggles every scan
 * rather than pulsing once.
 *
 * Indexing by the instruction's own program offset gives every PLS an
 * independent history with no collisions, and costs 2000 bytes for the full
 * 16000-step program space.
 */
#define PULSE_STATE_WORDS ((PLC_PROGRAM_STEPS + 31) / 32)

typedef struct {
    bool in_stl;
    bool accepting_sources;
    bool step_gate;
    uint16_t sources[STL_MAX_SOURCES];
    uint8_t source_count;
    uint32_t reset_at_end[STL_RESET_WORDS];
} stl_context_t;

static uint16_t unknown_count;
static uint16_t last_bad_opcode;
static uint32_t pulse_prev[PULSE_STATE_WORDS];

/*
 * Records the rung state at the instruction stored at word_off and reports
 * whether this scan is its edge - rising for PLS, falling for PLF. Steps
 * beyond the program space cannot hold an instruction, so they never pulse.
 */
static bool pulse_edge(uint32_t word_off, bool rung, bool falling) {
    uint32_t step = word_off / 2u;
    if (step >= PLC_PROGRAM_STEPS) {
        return false;
    }
    uint32_t mask = (uint32_t)1u << (step % 32u);
    bool previous = (pulse_prev[step / 32u] & mask) != 0;
    if (rung) {
        pulse_prev[step / 32u] |= mask;
    } else {
        pulse_prev[step / 32u] &= ~mask;
    }
    return falling ? (previous && !rung) : (rung && !previous);
}

static uint16_t fetch(uint32_t off) {
    return (uint16_t)(plc_program_read(off) | ((uint16_t)plc_program_read(off + 1) << 8));
}

static bool read_bit_operand(uint16_t lo, uint16_t hi, uint8_t *dev,
                             uint8_t *number);

static bool read_word_operand(uint16_t lo, uint16_t hi, uint16_t *value) {
    uint8_t type = (uint8_t)(lo >> 8);
    uint16_t raw = (uint16_t)((lo & 0xFF) | ((hi & 0xFF) << 8));
    if (type == OPERAND_D) {
        /* IEC timer FBs encode TN199 as 868E 8201: the 0x82 high type
         * distinguishes the timer-current word from ordinary D199. */
        if ((hi >> 8) == OPERAND_COMPARE_CONST && raw / 2u < PLC_NUM_T) {
            *value = plc_mem.t[raw / 2u].current;
        } else {
            *value = plc_get_d((uint16_t)(raw / 2u));
        }
        return true;
    }
    if (type == OPERAND_CONST || type == OPERAND_COMPARE_CONST) {
        *value = raw;
        return true;
    }
    return false;
}

static bool word_operand_register(uint16_t lo, uint16_t hi, uint16_t *reg) {
    uint16_t raw;
    if ((lo >> 8) != OPERAND_D ||
        ((hi >> 8) != OPERAND_D && (hi >> 8) != OPERAND_CONST)) return false;
    raw = (uint16_t)((lo & 0xFF) | ((hi & 0xFF) << 8));
    if ((raw & 1u) || raw / 2u >= PLC_NUM_D) return false;
    *reg = (uint16_t)(raw / 2u);
    return true;
}

/*
 * Destination of a 32-bit operation. Captured from three Structured Ladder
 * DMOV rungs whose only material difference is the destination:
 *
 *   86D0 8807   DMOV ... D2000   1000 + 0x07D0 / 2
 *   86D4 8807   DMOV ... D2002   1000 + 0x07D4 / 2
 *   8600 8800   DMOV ... D1000   1000 + 0x0000 / 2
 *
 * The 0x88 high type therefore selects the D1000 bank; the encoded address is
 * still an even byte offset. Ordinary 0x80 destinations remain byte offsets
 * from D0.
 */
static bool dword_operand_register(uint16_t lo, uint16_t hi, uint16_t *reg) {
    if ((lo >> 8) != OPERAND_D) {
        return false;
    }
    if ((hi >> 8) == OPERAND_POINTER) {
        uint16_t raw = (uint16_t)((lo & 0xFF) | ((hi & 0xFF) << 8));
        if (raw & 1u) return false;
        *reg = (uint16_t)(1000u + raw / 2u);
        return *reg < PLC_NUM_D;
    }
    return word_operand_register(lo, hi, reg);
}

static bool fetch_word_value(uint32_t *off, uint16_t *value) {
    uint16_t lo = fetch(*off), hi = fetch(*off + 2);
    *off += 4;
    return read_word_operand(lo, hi, value);
}

static bool fetch_word_register(uint32_t *off, uint16_t *reg) {
    uint16_t lo = fetch(*off), hi = fetch(*off + 2);
    *off += 4;
    return word_operand_register(lo, hi, reg);
}

static bool fetch_bit_device(uint32_t *off, uint8_t *dev, uint8_t *number) {
    uint16_t lo = fetch(*off), hi = fetch(*off + 2);
    *off += 4;
    return read_bit_operand(lo, hi, dev, number);
}

static bool comparison_result(uint16_t instruction, int16_t a, int16_t b) {
    switch (instruction & 0x000Eu) {
        case 0x0000: return a == b;
        case 0x0002: return a > b;
        case 0x0004: return a < b;
        case 0x0008: return a != b;
        case 0x000A: return a <= b;
        case 0x000C: return a >= b;
        default: return false;
    }
}

static bool is_inline_comparison(uint16_t word) {
    uint16_t family = (uint16_t)(word & 0xFFF0u);
    uint16_t relation = (uint16_t)(word & 0x000Eu);
    bool valid_relation = relation == 0 || relation == 2 || relation == 4 ||
                          relation == 8 || relation == 10 || relation == 12;
    return valid_relation &&
           (family == INLINE_LD_EQ || family == INLINE_AND_EQ ||
            family == INLINE_OR_EQ);
}

static uint32_t find_subroutine(uint8_t pointer) {
    uint16_t marker = (uint16_t)((OPCODE_SUBROUTINE << 8) | pointer);
    for (uint32_t pos = PLC_CODE_OFFSET; pos + 1 < PLC_PROGRAM_BYTES; pos += 2) {
        if (fetch(pos) == marker) return pos + 2;
    }
    return PLC_PROGRAM_BYTES;
}

static bool read_bit_operand(uint16_t lo, uint16_t hi, uint8_t *dev,
                             uint8_t *number) {
    uint8_t lo_type = (uint8_t)(lo >> 8);
    uint8_t hi_type = (uint8_t)(hi >> 8);
    /* Structured POUs use bit 5 in the low type and bit 3 in the high type
     * for compiler-owned BOOL variables. They do not change the captured
     * device nibble or point number. */
    if ((lo_type & (uint8_t)~0x20u) != OPERAND_BIT ||
        (hi_type != OPERAND_CONST && hi_type != OPERAND_POINTER)) {
        return false;
    }
    *number = (uint8_t)(lo & 0xFF);
    *dev = (uint8_t)(hi & 0xFF);
    return true;
}

static void stl_begin_step(stl_context_t *stl, uint16_t state, bool merge) {
    if (!merge) {
        stl->source_count = 0;
        stl->step_gate = true;
    }
    stl->in_stl = true;
    stl->accepting_sources = true;
    stl->step_gate = stl->step_gate && plc_get_s(state);
    if (stl->source_count < STL_MAX_SOURCES) {
        stl->sources[stl->source_count++] = state;
    } else {
        unknown_count++;
        last_bad_opcode = (uint16_t)((OPCODE_STL << 8) | (state & 0xFF));
    }
}

static void stl_queue_transfer(stl_context_t *stl) {
    for (uint8_t i = 0; i < stl->source_count; i++) {
        uint16_t state = stl->sources[i];
        if (state < PLC_NUM_S) {
            stl->reset_at_end[state / 32] |= (uint32_t)1u << (state % 32);
        }
    }
}

static void stl_finish_scan(stl_context_t *stl) {
    for (uint16_t state = 0; state < PLC_NUM_S; state++) {
        if ((stl->reset_at_end[state / 32] >> (state % 32)) & 1u) {
            plc_set_s(state, false);
        }
    }
}

/* Expands a device nibble and operand into a device number, applying the
 * 256-per-nibble extension for M and S. */
static uint16_t device_number(uint8_t dev, uint8_t n) {
    if (dev >= DEV_M && dev <= 0xD) {
        return (uint16_t)(n + 256u * (dev - DEV_M));
    }
    if (dev <= 0x3) {
        return (uint16_t)(n + 256u * dev);
    }
    return n;
}

static bool read_bit(uint8_t dev, uint8_t n) {
    uint16_t i = device_number(dev, n);
    switch (dev) {
        case DEV_X: return plc_get_x(i);
        case DEV_Y: return plc_get_y(i);
        case DEV_MS: return plc_get_m((uint16_t)(PLC_M_SPECIAL_BASE + n));
        case DEV_T: return i < PLC_NUM_T ? plc_mem.t[i].done : false;
        case DEV_C: return i < PLC_NUM_C ? plc_mem.c[i].done : false;
        default: break;
    }
    if (dev >= DEV_M && dev <= 0xD) return plc_get_m(i);
    if (dev <= 0x3) return plc_get_s(i);
    return false;
}

static void write_bit(uint8_t dev, uint8_t n, bool v) {
    uint16_t i = device_number(dev, n);
    switch (dev) {
        case DEV_Y: plc_set_y(i, v); return;
        case DEV_MS: plc_set_m((uint16_t)(PLC_M_SPECIAL_BASE + n), v); return;
        case DEV_X: plc_set_x(i, v); return; /* the scan overwrites it next cycle */
        default: break;
    }
    if (dev >= DEV_M && dev <= 0xD) plc_set_m(i, v);
    else if (dev <= 0x3) plc_set_s(i, v);
}

static void bad_instruction(uint16_t word) {
    unknown_count++;
    last_bad_opcode = word;
}

/* Executes the ordinary (non-D/P) 16-bit forms of the requested applied
 * instructions. Their instruction word is 0x0010 + 2 * FNC. */
static bool execute_applied(uint16_t instruction, uint32_t *off, bool enabled) {
    uint16_t a, b, n, dst;
    uint8_t sdev, snum, ddev, dnum;
    bool valid = true;

    switch (instruction) {
        case MISC_DMOV:
        {
            uint16_t s0_lo = fetch(*off), s0_hi = fetch(*off + 2);
            uint16_t s1_lo = fetch(*off + 4), s1_hi = fetch(*off + 6);
            uint16_t d0_lo = fetch(*off + 8), d0_hi = fetch(*off + 10);
            uint16_t d1_lo = fetch(*off + 12), d1_hi = fetch(*off + 14);
            uint16_t value_lo, value_hi, padding;
            *off += 16;
            /* The destination's second pair is constant zero padding, in both
             * the IEC timer capture and the Structured Ladder one - the high
             * register is never named. */
            valid = read_word_operand(s0_lo, s0_hi, &value_lo) &&
                    read_word_operand(s1_lo, s1_hi, &value_hi) &&
                    dword_operand_register(d0_lo, d0_hi, &dst) &&
                    read_word_operand(d1_lo, d1_hi, &padding) && padding == 0 &&
                    dst + 1u < PLC_NUM_D;
            if (valid && enabled) {
                plc_set_d32(dst, (uint32_t)value_lo | ((uint32_t)value_hi << 16));
            }
            break;
        }
        case MISC_DDIV_TIME:
        {
            uint16_t s0_lo = fetch(*off), s0_hi = fetch(*off + 2);
            uint16_t s1_lo = fetch(*off + 4), s1_hi = fetch(*off + 6);
            uint16_t b0_lo = fetch(*off + 8), b0_hi = fetch(*off + 10);
            uint16_t b1_lo = fetch(*off + 12), b1_hi = fetch(*off + 14);
            uint16_t d0_lo = fetch(*off + 16), d0_hi = fetch(*off + 18);
            uint16_t d1_lo = fetch(*off + 20), d1_hi = fetch(*off + 22);
            uint16_t a_lo, a_hi, b_lo, b_hi;
            *off += 24;
            valid = read_word_operand(s0_lo, s0_hi, &a_lo) &&
                    read_word_operand(s1_lo, s1_hi, &a_hi) &&
                    read_word_operand(b0_lo, b0_hi, &b_lo) &&
                    read_word_operand(b1_lo, b1_hi, &b_hi) &&
                    word_operand_register(d0_lo, d0_hi, &dst) &&
                    read_word_operand(d1_lo, d1_hi, &n) && n == 0 &&
                    dst + 3u < PLC_NUM_D;
            uint32_t dividend = (uint32_t)a_lo | ((uint32_t)a_hi << 16);
            uint32_t divisor = (uint32_t)b_lo | ((uint32_t)b_hi << 16);
            if (divisor == 0) valid = false;
            if (valid && enabled) {
                plc_set_d32(dst, dividend / divisor);
                plc_set_d32((uint16_t)(dst + 2u), dividend % divisor);
            }
            break;
        }
        case MISC_CMP:
            valid = fetch_word_value(off, &a) && fetch_word_value(off, &b) &&
                    fetch_bit_device(off, &ddev, &dnum) && dnum <= 253;
            if (valid && enabled) {
                write_bit(ddev, dnum, (int16_t)a > (int16_t)b);
                write_bit(ddev, (uint8_t)(dnum + 1), (int16_t)a == (int16_t)b);
                write_bit(ddev, (uint8_t)(dnum + 2), (int16_t)a < (int16_t)b);
            }
            break;
        case MISC_BMOV:
            valid = fetch_word_register(off, &a) && fetch_word_register(off, &dst) &&
                    fetch_word_value(off, &n) && n <= PLC_NUM_D &&
                    (uint32_t)a + n <= PLC_NUM_D && (uint32_t)dst + n <= PLC_NUM_D;
            if (valid && enabled) {
                if (dst > a && dst < a + n) {
                    for (uint16_t i = n; i-- > 0;) plc_set_d((uint16_t)(dst + i), plc_get_d((uint16_t)(a + i)));
                } else {
                    for (uint16_t i = 0; i < n; i++) plc_set_d((uint16_t)(dst + i), plc_get_d((uint16_t)(a + i)));
                }
            }
            break;
        case MISC_FMOV:
            valid = fetch_word_value(off, &a) && fetch_word_register(off, &dst) &&
                    fetch_word_value(off, &n) && (uint32_t)dst + n <= PLC_NUM_D;
            if (valid && enabled) for (uint16_t i = 0; i < n; i++) plc_set_d((uint16_t)(dst + i), a);
            break;
        case MISC_ADD: case MISC_SUB: case MISC_MUL: case MISC_DIV:
        case MISC_WAND: case MISC_WOR: case MISC_WXOR:
            valid = fetch_word_value(off, &a) && fetch_word_value(off, &b) && fetch_word_register(off, &dst);
            if (valid && instruction == MISC_MUL && dst + 1u >= PLC_NUM_D) valid = false;
            if (valid && instruction == MISC_DIV && dst + 1u >= PLC_NUM_D) valid = false;
            if (valid && instruction == MISC_DIV && b == 0) valid = false;
            if (valid && enabled) {
                switch (instruction) {
                    case MISC_ADD: plc_set_d(dst, (uint16_t)(a + b)); break;
                    case MISC_SUB: plc_set_d(dst, (uint16_t)(a - b)); break;
                    case MISC_MUL: plc_set_d32(dst, (uint32_t)((int32_t)(int16_t)a * (int32_t)(int16_t)b)); break;
                    case MISC_DIV:
                        plc_set_d(dst, (uint16_t)((int16_t)a / (int16_t)b));
                        plc_set_d((uint16_t)(dst + 1), (uint16_t)((int16_t)a % (int16_t)b));
                        break;
                    case MISC_WAND: plc_set_d(dst, (uint16_t)(a & b)); break;
                    case MISC_WOR: plc_set_d(dst, (uint16_t)(a | b)); break;
                    default: plc_set_d(dst, (uint16_t)(a ^ b)); break;
                }
            }
            break;
        case MISC_INC: case MISC_DEC: case MISC_NEG:
            valid = fetch_word_register(off, &dst);
            if (valid && enabled) {
                uint16_t v = plc_get_d(dst);
                plc_set_d(dst, instruction == MISC_INC ? (uint16_t)(v + 1) :
                              instruction == MISC_DEC ? (uint16_t)(v - 1) :
                              (uint16_t)(-(int16_t)v));
            }
            break;
        case MISC_ROR: case MISC_ROL:
            valid = fetch_word_register(off, &dst) && fetch_word_value(off, &n);
            if (valid && enabled) {
                uint16_t v = plc_get_d(dst);
                n &= 15u;
                if (n) plc_set_d(dst, instruction == MISC_ROR ?
                    (uint16_t)((v >> n) | (v << (16u - n))) :
                    (uint16_t)((v << n) | (v >> (16u - n))));
            }
            break;
        case MISC_SFTR:
            valid = fetch_bit_device(off, &sdev, &snum) && fetch_bit_device(off, &ddev, &dnum) &&
                    fetch_word_value(off, &n) && fetch_word_value(off, &b) &&
                    n > 0 && b <= n && (uint32_t)dnum + n <= 256u;
            if (valid && enabled) {
                bool source = read_bit(sdev, snum);
                for (uint16_t pass = 0; pass < b; pass++) {
                    for (uint16_t i = 0; i + 1 < n; i++)
                        write_bit(ddev, (uint8_t)(dnum + i), read_bit(ddev, (uint8_t)(dnum + i + 1)));
                    write_bit(ddev, (uint8_t)(dnum + n - 1), pass == 0 ? source : false);
                }
            }
            break;
        case MISC_SFWR:
            valid = fetch_word_value(off, &a) && fetch_word_register(off, &dst) &&
                    fetch_word_value(off, &n) && n >= 2 && (uint32_t)dst + n <= PLC_NUM_D;
            if (valid && enabled) {
                uint16_t pointer = plc_get_d(dst);
                bool full = pointer >= n - 1u;
                plc_set_m(8022, full);
                if (!full) {
                    pointer++;
                    plc_set_d(dst, pointer);
                    plc_set_d((uint16_t)(dst + pointer), a);
                }
            }
            break;
        case MISC_SFRD:
            valid = fetch_word_register(off, &a) && fetch_word_register(off, &dst) &&
                    fetch_word_value(off, &n) && n >= 2 && (uint32_t)a + n <= PLC_NUM_D;
            if (valid && enabled) {
                uint16_t pointer = plc_get_d(a);
                plc_set_m(8020, pointer == 0);
                if (pointer) {
                    plc_set_d(dst, plc_get_d((uint16_t)(a + 1)));
                    for (uint16_t i = 1; i < pointer; i++)
                        plc_set_d((uint16_t)(a + i), plc_get_d((uint16_t)(a + i + 1)));
                    plc_set_d(a, (uint16_t)(pointer - 1));
                }
            }
            break;
        case MISC_ENCO:
        {
            uint16_t s_lo = fetch(*off), s_hi = fetch(*off + 2);
            bool word_source;
            *off += 4;
            word_source = read_word_operand(s_lo, s_hi, &a);
            if (!word_source) valid = read_bit_operand(s_lo, s_hi, &sdev, &snum);
            valid = valid && fetch_word_register(off, &dst) &&
                    fetch_word_value(off, &n) && n >= 1 && n <= 4 &&
                    (word_source || (uint32_t)snum + (1u << n) <= 256u);
            if (valid && enabled) {
                uint16_t limit = (uint16_t)(1u << n), encoded = 0;
                for (uint16_t i = 0; i < limit; i++) {
                    bool on = word_source ? ((a >> i) & 1u) != 0 :
                                             read_bit(sdev, (uint8_t)(snum + i));
                    if (on) encoded = i;
                }
                plc_set_d(dst, encoded);
            }
            break;
        }
        case MISC_BON:
        {
            uint16_t s_lo = fetch(*off), s_hi = fetch(*off + 2);
            uint16_t d_lo = fetch(*off + 4), d_hi = fetch(*off + 6);
            *off += 8;
            bool source_is_group = ((s_lo >> 8) & 0x20u) != 0;
            bool destination_is_group = ((d_lo >> 8) & 0x20u) != 0;
            uint16_t array_index = plc_get_d(28);
            valid = read_bit_operand(s_lo, s_hi, &sdev, &snum) &&
                    read_bit_operand(d_lo, d_hi, &ddev, &dnum) &&
                    fetch_word_value(off, &n) && n <= 15 &&
                    (uint32_t)snum + (source_is_group ? array_index + n : 0u) <= 255u &&
                    (uint32_t)dnum + (destination_is_group ? array_index + n : 0u) <= 255u;
            if (valid && enabled) {
                if (destination_is_group && !source_is_group) {
                    /* Structured FB code copies StpNum to D28 immediately
                     * before BON.  A4 marks an array operand indexed through
                     * that implicit compiler register. */
                    write_bit(ddev, (uint8_t)(dnum + array_index + n),
                              read_bit(sdev, snum));
                } else {
                    write_bit(ddev, dnum,
                              read_bit(sdev, (uint8_t)(snum + n +
                                                       (source_is_group ? array_index : 0u))));
                }
            }
            break;
        }
        case MISC_FLT:
            valid = fetch_word_value(off, &a) && fetch_word_register(off, &dst) && dst + 1u < PLC_NUM_D;
            if (valid && enabled) {
                float f = (float)(int16_t)a;
                uint32_t bits;
                memcpy(&bits, &f, sizeof(bits));
                plc_set_d32(dst, bits);
            }
            break;
        default:
            return false;
    }
    if (!valid) bad_instruction(instruction);
    return true;
}

bool plc_exec_has_program(void) {
    for (uint32_t off = PLC_CODE_OFFSET; off + 1 < PLC_PROGRAM_BYTES; off += 2) {
        uint16_t w = fetch(off);
        if (w == WORD_BLANK) {
            continue;
        }
        if ((w >> 8) == 0x00 && (w & 0xFF) == MISC_END) {
            return true;
        }
    }
    return false;
}

uint16_t plc_exec_unknown_count(void) { return unknown_count; }
uint16_t plc_exec_last_bad_opcode(void) { return last_bad_opcode; }

void plc_exec_scan(void) {
    /*
     * Rung state. `result` is the running logical result of the current rung -
     * what a ladder diagram draws as the power flowing left to right. LD
     * starts a rung, AND/OR combine into it, OUT/SET/RST consume it.
     *
     * `pulse_pending` records that a pulse prefix was seen, so the next device
     * word is a PLS or PLF rather than a plain coil. It holds the prefix
     * operand itself, or 0 when no pulse is pending.
     */
    bool result = false;
    uint8_t pulse_pending = 0;
    stl_context_t stl = {0};

    /*
     * MPS/MRD/MPP branch stack, and the block stack used by ANB/ORB. A real FX
     * allows 11 levels of MPS nesting. LD always pushes the previous rung
     * result onto the block stack so that a later ANB/ORB has something to
     * combine with; entries nobody pops are simply discarded when the stack
     * wraps, which is what makes plain rung-after-rung code safe.
     */
    bool branch[16];
    uint8_t branch_sp = 0;
    bool block[16];
    uint8_t block_sp = 0;
    uint32_t call_return[CALL_STACK_DEPTH];
    uint8_t call_sp = 0;

    unknown_count = 0;

    /* A STOP -> RUN transition restarts the program, so no instruction has a
     * previous rung state to compare against. Without this, a PLS whose rung
     * was true when the PLC stopped would not fire on the first scan after it
     * runs again - including after a download, which stops the PLC. */
    if (plc_scan_is_first()) {
        memset(pulse_prev, 0, sizeof pulse_prev);
    }

    uint32_t off = PLC_CODE_OFFSET;
    while (off + 1 < PLC_PROGRAM_BYTES) {
        uint16_t word = fetch(off);
        off += 2;

        if (word == WORD_BLANK) {
            continue; /* unwritten gap between download blocks */
        }

        uint8_t opcode = (uint8_t)(word >> 8);
        uint8_t operand = (uint8_t)(word & 0xFF);
        uint8_t op = (uint8_t)(opcode >> 4);
        uint8_t dev = (uint8_t)(opcode & 0x0F);

        bool consecutive_stl = stl.in_stl && stl.accepting_sources;
        if (opcode != OPCODE_STL) {
            stl.accepting_sources = false;
        }

        /* Preset coils: the next two words carry a 32-bit constant. */
        if (opcode == OPCODE_OUT_T || opcode == OPCODE_OUT_C) {
            uint16_t lo = fetch(off);
            uint16_t hi = fetch(off + 2);
            off += 4;
            uint16_t preset;
            if (!read_word_operand(lo, hi, &preset)) {
                bad_instruction(word);
                continue;
            }

            if (opcode == OPCODE_OUT_T) {
                plc_timer_drive(operand, preset, result);
            } else {
                plc_counter_drive(operand, (int32_t)preset, result);
            }
            block_sp = 0;
            continue;
        }

        if (opcode == 0x00) {
            if (operand == MISC_END || operand == MISC_FEND) {
                stl_finish_scan(&stl);
                return;
            }
            if (operand == MISC_CALL) {
                uint16_t target_lo = fetch(off), target_hi = fetch(off + 2);
                off += 4;
                bool valid = (target_lo >> 8) == OPERAND_POINTER &&
                             (target_hi >> 8) == OPERAND_CONST &&
                             ((target_lo & 0xFF) & 1u) == 0;
                uint8_t pointer = (uint8_t)((target_lo & 0xFF) / 2u);
                uint32_t target = valid ? find_subroutine(pointer) : PLC_PROGRAM_BYTES;
                if (!valid || target >= PLC_PROGRAM_BYTES ||
                    (result && call_sp >= CALL_STACK_DEPTH)) {
                    bad_instruction(word);
                } else if (result) {
                    call_return[call_sp++] = off;
                    off = target;
                }
                continue;
            }
            if (operand == MISC_SRET) {
                if (call_sp) {
                    off = call_return[--call_sp];
                } else {
                    bad_instruction(word);
                }
                continue;
            }
            if (execute_applied(operand, &off, result)) {
                continue;
            }
            if (operand == MISC_PLS_PREFIX || operand == MISC_PLF_PREFIX) {
                /*
                 * A pulse prefix only means "pulse" when a device word really
                 * follows it. Op nibble 8 is shared with the constant marker,
                 * so opcode 0x80 exactly is excluded; anything else is
                 * rejected outright rather than left pending, which would
                 * otherwise let a misread prefix attach itself to a genuine
                 * pulse coil further down the program.
                 */
                uint16_t next = fetch(off);
                uint8_t next_opcode = (uint8_t)(next >> 8);
                if ((next_opcode >> 4) != OP_PLS || next_opcode == OPCODE_CONST) {
                    bad_instruction(word);
                    continue;
                }
                pulse_pending = operand;
                continue;
            }
            if (operand == MISC_RST_C) {
                /* Captured structured reset forms:
                 *   000C 8E00  RESET C0
                 *   000C 86C6  RESET T198 (IEC TOF instance) */
                uint16_t target = fetch(off);
                off += 2;
                if ((target >> 8) == 0x8E) {
                    if (result) plc_counter_reset((uint8_t)(target & 0xFF));
                } else if ((target >> 8) == OPERAND_D) {
                    if (result) plc_timer_reset((uint8_t)(target & 0xFF));
                } else {
                    unknown_count++;
                    last_bad_opcode = word;
                }
                continue;
            }
            if (operand == MISC_SET_S) {
                /* One value word follows carrying the state number. */
                uint16_t v = fetch(off);
                off += 2;
                if (result) {
                    uint16_t state = (uint16_t)(v & 0xFF);
                    plc_set_s(state, true);
                    if (stl.in_stl) stl_queue_transfer(&stl);
                }
                continue;
            }
            if (operand == MISC_RST_S) {
                /* Captured in FSM_SR_NO_SC: reset the named state when the
                 * rung is true. Without this, each transition accumulated
                 * another active S bit. */
                uint16_t v = fetch(off);
                off += 2;
                if (result) plc_set_s((uint16_t)(v & 0xFF), false);
                continue;
            }
            if (operand == MISC_OUT_S) {
                /* Captured in FSM_PN: OUT S is followed by one constant word
                 * containing the state number. */
                uint16_t v = fetch(off);
                off += 2;
                plc_set_s((uint16_t)(v & 0xFF), result);
                continue;
            }
            if (operand == MISC_ZRST) {
                /* Captured as 0060 8400 8005 8403 8005 for ZRST Y0 Y3.
                 * Each typed bit operand is (0x84,index), (0x80,device).
                 * Consume the complete instruction even when malformed so
                 * operand words are never executed as standalone opcodes. */
                uint16_t a_lo = fetch(off), a_hi = fetch(off + 2);
                uint16_t b_lo = fetch(off + 4), b_hi = fetch(off + 6);
                off += 8;
                if ((a_lo >> 8) == OPERAND_BIT &&
                    (b_lo >> 8) == OPERAND_BIT &&
                    (a_hi >> 8) == OPERAND_CONST &&
                    (b_hi >> 8) == OPERAND_CONST &&
                    (a_hi & 0xFF) == (b_hi & 0xFF)) {
                    uint8_t bit_dev = (uint8_t)(a_hi & 0xFF);
                    uint16_t first = (uint16_t)(a_lo & 0xFF);
                    uint16_t last = (uint16_t)(b_lo & 0xFF);
                    if (result && first <= last) {
                        for (uint16_t i = first; i <= last; i++) {
                            write_bit(bit_dev, (uint8_t)i, false);
                        }
                    }
                } else {
                    unknown_count++;
                    last_bad_opcode = word;
                }
                continue;
            }
            if (operand == MISC_SFTL) {
                /* FSM_SHL capture:
                 *   0056 84F3 800D 840A 8000 8009 8000 8001 8000
                 *   SFTL M1523 S10 K9 K1
                 * Shift the destination range toward higher device numbers,
                 * loading the source into its first bit. */
                uint16_t s_lo = fetch(off), s_hi = fetch(off + 2);
                uint16_t d_lo = fetch(off + 4), d_hi = fetch(off + 6);
                uint16_t n_lo = fetch(off + 8), n_hi = fetch(off + 10);
                uint16_t k_lo = fetch(off + 12), k_hi = fetch(off + 14);
                off += 16;
                uint8_t s_dev, s_num, d_dev, d_num;
                uint16_t length, shifts;
                if (!read_bit_operand(s_lo, s_hi, &s_dev, &s_num) ||
                    !read_bit_operand(d_lo, d_hi, &d_dev, &d_num) ||
                    !read_word_operand(n_lo, n_hi, &length) ||
                    !read_word_operand(k_lo, k_hi, &shifts) ||
                    length == 0 || shifts > length ||
                    (uint32_t)d_num + length > 256u) {
                    unknown_count++;
                    last_bad_opcode = word;
                    continue;
                }
                if (result) {
                    bool source = read_bit(s_dev, s_num);
                    for (uint16_t pass = 0; pass < shifts; pass++) {
                        for (uint16_t i = length - 1; i > 0; i--) {
                            write_bit(d_dev, (uint8_t)(d_num + i),
                                      read_bit(d_dev, (uint8_t)(d_num + i - 1)));
                        }
                        write_bit(d_dev, d_num, pass == 0 ? source : false);
                    }
                }
                continue;
            }
            if (operand == MISC_ABSD) {
                /* Absolute drum sequencer (FNC 62), captured as:
                 *   008C 8658 8602 8600 8400 8400 8005 8004 8000
                 *   ABSD D300 C0 Y0 K4
                 * D300/D301 are Y0's ON/OFF thresholds, followed by one
                 * consecutive pair for each output. */
                uint16_t table_lo = fetch(off), table_hi = fetch(off + 2);
                uint16_t ctr_lo = fetch(off + 4), ctr_hi = fetch(off + 6);
                uint16_t dst_lo = fetch(off + 8), dst_hi = fetch(off + 10);
                uint16_t count_lo = fetch(off + 12), count_hi = fetch(off + 14);
                off += 16;

                uint8_t dst_dev, dst_num;
                uint16_t count;
                uint16_t table_raw = (uint16_t)((table_lo & 0xFF) |
                                                ((table_hi & 0xFF) << 8));
                uint16_t table = (uint16_t)(table_raw / 2u);
                bool valid = (table_lo >> 8) == OPERAND_D &&
                             (table_hi >> 8) == OPERAND_D &&
                             (ctr_lo >> 8) == OPERAND_D &&
                             (ctr_hi >> 8) == OPERAND_BIT &&
                             (ctr_hi & 0xFF) == 0 &&
                             read_bit_operand(dst_lo, dst_hi, &dst_dev, &dst_num) &&
                             read_word_operand(count_lo, count_hi, &count) &&
                             count <= 64 &&
                             (uint32_t)table + 2u * count <= PLC_NUM_D &&
                             (uint32_t)dst_num + count <= 256u;
                uint16_t counter = (uint16_t)(ctr_lo & 0xFF);
                if (!valid || counter >= PLC_NUM_C) {
                    unknown_count++;
                    last_bad_opcode = word;
                    continue;
                }
                if (result) {
                    int32_t current = plc_mem.c[counter].current;
                    for (uint16_t i = 0; i < count; i++) {
                        uint16_t on_at = plc_get_d((uint16_t)(table + 2u * i));
                        uint16_t off_at = plc_get_d((uint16_t)(table + 2u * i + 1u));
                        write_bit(dst_dev, (uint8_t)(dst_num + i),
                                  current >= on_at && current < off_at);
                    }
                }
                continue;
            }
            if (operand == MISC_DECO) {
                /* FSM_Counter_Decode capture:
                 *   0062 8600 8400 8409 8000 8004 8000
                 *   DECO C0 S9 K4
                 * A width of n source bits drives 2^n consecutive one-hot
                 * destination bits. */
                uint16_t src_lo = fetch(off), src_hi = fetch(off + 2);
                uint16_t dst_lo = fetch(off + 4), dst_hi = fetch(off + 6);
                uint16_t n_lo = fetch(off + 8), n_hi = fetch(off + 10);
                off += 12;

                uint8_t dst_dev, dst_num;
                uint16_t width;
                bool source_is_counter = (src_lo >> 8) == OPERAND_D &&
                                         (src_hi >> 8) == OPERAND_BIT &&
                                         (src_hi & 0xFF) == 0;
                uint16_t counter = (uint16_t)(src_lo & 0xFF);
                bool valid = source_is_counter && counter < PLC_NUM_C &&
                             read_bit_operand(dst_lo, dst_hi, &dst_dev, &dst_num) &&
                             read_word_operand(n_lo, n_hi, &width) &&
                             width >= 1 && width <= 8;
                uint16_t destinations = valid ? (uint16_t)(1u << width) : 0;
                if (!valid || (uint32_t)dst_num + destinations > 256u) {
                    unknown_count++;
                    last_bad_opcode = word;
                    continue;
                }
                if (result) {
                    int32_t selected = plc_mem.c[counter].current;
                    for (uint16_t i = 0; i < destinations; i++) {
                        write_bit(dst_dev, (uint8_t)(dst_num + i),
                                  selected >= 0 && (uint32_t)selected == i);
                    }
                }
                continue;
            }
            if (operand == MISC_MOV) {
                /* Two typed operand pairs: source then destination. A D
                 * operand carries a byte address, hence the halving. */
                uint16_t s_lo = fetch(off), s_hi = fetch(off + 2);
                uint16_t d_lo = fetch(off + 4), d_hi = fetch(off + 6);
                off += 8;
                uint8_t s_dev, s_num, d_dev, d_num;
                if (read_bit_operand(s_lo, s_hi, &s_dev, &s_num) &&
                    read_bit_operand(d_lo, d_hi, &d_dev, &d_num)) {
                    if (result) {
                        /* GX Works2 uses this MOV form for ARRAY [0..15] OF
                         * BOOL parameters. Copy a packed word, with a
                         * temporary so overlapping compiler VAR areas work. */
                        bool bits[16];
                        for (uint8_t i = 0; i < 16; i++)
                            bits[i] = read_bit(s_dev, (uint8_t)(s_num + i));
                        for (uint8_t i = 0; i < 16; i++)
                            write_bit(d_dev, (uint8_t)(d_num + i), bits[i]);
                    }
                    continue;
                }
                uint16_t value;
                if ((s_lo >> 8) == OPERAND_D) {
                    uint16_t reg = (uint16_t)(((s_lo & 0xFF) | ((s_hi & 0xFF) << 8)) / 2);
                    value = plc_get_d(reg);
                } else {
                    value = (uint16_t)((s_lo & 0xFF) | ((s_hi & 0xFF) << 8));
                }
                if (result && (d_lo >> 8) == OPERAND_D) {
                    uint16_t reg = (uint16_t)(((d_lo & 0xFF) | ((d_hi & 0xFF) << 8)) / 2);
                    plc_set_d(reg, value);
                }
                continue;
            }
            unknown_count++;
            last_bad_opcode = word;
            continue;
        }

        /* Stack and block operations share opcode 0xFF. */
        if (opcode == OPCODE_STACK) {
            switch (operand) {
                case STACK_MPS:
                    if (branch_sp < 16) branch[branch_sp++] = result;
                    break;
                case STACK_MRD:
                    if (branch_sp) result = branch[branch_sp - 1];
                    break;
                case STACK_MPP:
                    if (branch_sp) result = branch[--branch_sp];
                    break;
                case STACK_ANB:
                    if (block_sp) result = block[--block_sp] && result;
                    break;
                case STACK_ORB:
                    if (block_sp) result = block[--block_sp] || result;
                    break;
                default:
                    unknown_count++;
                    last_bad_opcode = word;
                    break;
            }
            continue;
        }

        /* STL creates a local bus gated by its state. Consecutive STL
         * instructions form a multiple-state merge (logical AND). */
        if (opcode == OPCODE_STL) {
            stl_begin_step(&stl, operand, consecutive_stl);
            result = stl.step_gate;
            continue;
        }
        if (opcode == OPCODE_RET) {
            stl.in_stl = false;
            stl.accepting_sources = false;
            stl.source_count = 0;
            stl.step_gate = true;
            result = false;
            continue;
        }

        if (opcode == OPCODE_CONST) {
            continue; /* stray constant - already consumed by its instruction */
        }

        /* FSM_EQU capture: D001 <typed lhs> <typed rhs>. EQ_E is a
         * comparison contact/block and starts its network with the equality
         * result; the following automatic M coil consumes that result. */
        if (is_inline_comparison(word)) {
            uint16_t a_lo = fetch(off), a_hi = fetch(off + 2);
            uint16_t b_lo = fetch(off + 4), b_hi = fetch(off + 6);
            off += 8;
            uint16_t a, b;
            if (read_word_operand(a_lo, a_hi, &a) &&
                read_word_operand(b_lo, b_hi, &b)) {
                bool comparison = comparison_result(word, (int16_t)a, (int16_t)b);
                switch (word & 0xFFF0u) {
                    case INLINE_AND_EQ: result = result && comparison; break;
                    case INLINE_OR_EQ: result = result || comparison; break;
                    default: result = comparison; break;
                }
            } else {
                result = false;
                unknown_count++;
                last_bad_opcode = word;
            }
            continue;
        }

        if (word == OUT_C_ENABLE_WORD) {
            /* FSM_DRUM OUT_C wrapper: 01CA 8400, then the normal
             * 0E00 8009 8000 counter coil and preset. */
            uint16_t enable = fetch(off);
            off += 2;
            if ((enable >> 8) == 0x84) {
                result = read_bit(DEV_X, (uint8_t)(enable & 0xFF));
            } else {
                result = false;
                unknown_count++;
                last_bad_opcode = word;
            }
            continue;
        }

        switch (op) {
            case OP_LD:
                if (block_sp < 16) block[block_sp++] = result;
                result = (!stl.in_stl || stl.step_gate) && read_bit(dev, operand);
                break;
            case OP_LDI:
                if (block_sp < 16) block[block_sp++] = result;
                result = (!stl.in_stl || stl.step_gate) && !read_bit(dev, operand);
                break;
            case OP_AND: result = result && read_bit(dev, operand); break;
            case OP_ANI: result = result && !read_bit(dev, operand); break;
            case OP_OR:
                result = result || ((!stl.in_stl || stl.step_gate) && read_bit(dev, operand));
                break;
            case OP_ORI:
                result = result || ((!stl.in_stl || stl.step_gate) && !read_bit(dev, operand));
                break;

            case OP_OUT:
                if (stl.in_stl && dev <= 0x3) {
                    /* OUT S is a transfer inside STL, not an ordinary coil. */
                    if (result) {
                        write_bit(dev, operand, true);
                        stl_queue_transfer(&stl);
                    }
                } else {
                    write_bit(dev, operand, result);
                }
                block_sp = 0;
                break;

            case OP_SET:
                if (result) {
                    write_bit(dev, operand, true);
                    if (stl.in_stl && dev <= 0x3) stl_queue_transfer(&stl);
                }
                block_sp = 0;
                break;

            case OP_RST:
                if (result) {
                    write_bit(dev, operand, false);
                    if (dev == DEV_T) plc_timer_reset(operand);
                    if (dev == DEV_C) plc_counter_reset(operand);
                }
                block_sp = 0;
                break;

            case OP_PLS:
                /* Only valid straight after the 0x0008 prefix; the same
                 * nibble means "constant" otherwise, which is why the prefix
                 * has to gate it. */
                if (pulse_pending) {
                    bool falling = pulse_pending == MISC_PLF_PREFIX;
                    pulse_pending = 0;
                    /* Set for exactly one scan on the rung's edge - rising for
                     * PLS, falling for PLF. The edge is measured from the rung
                     * at this instruction, remembered per program step; see
                     * pulse_edge(). `off` has already advanced past this
                     * device word. */
                    write_bit(dev, operand, pulse_edge(off - 2u, result, falling));
                } else {
                    unknown_count++;
                    last_bad_opcode = word;
                }
                block_sp = 0;
                break;

            default:
                unknown_count++;
                last_bad_opcode = word;
                break;
        }
    }

    stl_finish_scan(&stl);
}
