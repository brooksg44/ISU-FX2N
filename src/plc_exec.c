#include "plc_exec.h"

#include "plc_memory.h"
#include "plc_program.h"
#include "plc_scan.h"

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
#define MISC_END 0x0F
#define MISC_FEND 0x1C /* program body terminator emitted before END */
#define MISC_MOV 0x28 /* followed by two typed operand pairs */
#define MISC_SFTL 0x56 /* source bit, destination bit, length, shift count */
#define MISC_ZRST 0x60 /* followed by two typed bit-device operand pairs */

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

/* Structured Ladder EQ_E-2 prefix captured as bytes D0 01 in FSM_EQU. The
 * program image is fetched little-endian, hence instruction word 0x01D0. */
#define EQ_E_WORD 0x01D0

/* Preset coils live in the misc group with a device nibble. */
#define OPCODE_OUT_T 0x06
#define OPCODE_OUT_C 0x0E
#define OPCODE_CONST 0x80

#define WORD_BLANK 0xFFFF

#define STL_MAX_SOURCES 8
#define STL_RESET_WORDS ((PLC_NUM_S + 31) / 32)

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

static uint16_t fetch(uint32_t off) {
    return (uint16_t)(plc_program_read(off) | ((uint16_t)plc_program_read(off + 1) << 8));
}

static bool read_word_operand(uint16_t lo, uint16_t hi, uint16_t *value) {
    uint8_t type = (uint8_t)(lo >> 8);
    uint16_t raw = (uint16_t)((lo & 0xFF) | ((hi & 0xFF) << 8));
    if (type == OPERAND_D) {
        *value = plc_get_d((uint16_t)(raw / 2u));
        return true;
    }
    if (type == OPERAND_CONST || type == OPERAND_COMPARE_CONST) {
        *value = raw;
        return true;
    }
    return false;
}

static bool read_bit_operand(uint16_t lo, uint16_t hi, uint8_t *dev,
                             uint8_t *number) {
    if ((lo >> 8) != OPERAND_BIT || (hi >> 8) != OPERAND_CONST) {
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
     * `pls_pending` records that a 0x0008 prefix was seen, so the next device
     * word is a PLS rather than a plain coil.
     */
    bool result = false;
    bool pls_pending = false;
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

    unknown_count = 0;

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
            uint32_t preset = (uint32_t)(lo & 0xFF) | ((uint32_t)(hi & 0xFF) << 8);

            if (opcode == OPCODE_OUT_T) {
                plc_timer_drive(operand, (uint16_t)preset, result);
            } else {
                plc_counter_drive(operand, (int32_t)preset, result);
            }
            continue;
        }

        if (opcode == 0x00) {
            if (operand == MISC_END || operand == MISC_FEND) {
                stl_finish_scan(&stl);
                return;
            }
            if (operand == MISC_PLS_PREFIX) {
                pls_pending = true;
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
            if (operand == MISC_MOV) {
                /* Two typed operand pairs: source then destination. A D
                 * operand carries a byte address, hence the halving. */
                uint16_t s_lo = fetch(off), s_hi = fetch(off + 2);
                uint16_t d_lo = fetch(off + 4), d_hi = fetch(off + 6);
                off += 8;
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
        if (word == EQ_E_WORD) {
            uint16_t a_lo = fetch(off), a_hi = fetch(off + 2);
            uint16_t b_lo = fetch(off + 4), b_hi = fetch(off + 6);
            off += 8;
            uint16_t a, b;
            if (read_word_operand(a_lo, a_hi, &a) &&
                read_word_operand(b_lo, b_hi, &b)) {
                result = a == b;
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
                break;

            case OP_SET:
                if (result) {
                    write_bit(dev, operand, true);
                    if (stl.in_stl && dev <= 0x3) stl_queue_transfer(&stl);
                }
                break;

            case OP_RST:
                if (result) {
                    write_bit(dev, operand, false);
                    if (dev == DEV_T) plc_timer_reset(operand);
                    if (dev == DEV_C) plc_counter_reset(operand);
                }
                break;

            case OP_PLS:
                /* Only valid straight after the 0x0008 prefix; the same
                 * nibble means "constant" otherwise, which is why the prefix
                 * has to gate it. */
                if (pls_pending) {
                    pls_pending = false;
                    /* Rising-edge pulse: set for exactly one scan. Edge state
                     * is kept in the device itself, so a repeat scan with the
                     * rung still true will not re-pulse. */
                    write_bit(dev, operand, result && !read_bit(dev, operand));
                } else {
                    unknown_count++;
                    last_bad_opcode = word;
                }
                break;

            default:
                unknown_count++;
                last_bad_opcode = word;
                break;
        }
    }

    stl_finish_scan(&stl);
}
