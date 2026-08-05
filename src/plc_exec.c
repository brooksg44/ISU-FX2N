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
 * i.e. nibble 9 = M + 256, 256 + 44 = 300. So M spans nibbles 8..B and S
 * spans 0..3. Only 8, 9 and 0 have been observed; the rest follow the pattern.
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
#define MISC_SET_S 0x06 /* followed by one value word holding the S number */
#define MISC_PLS_PREFIX 0x08
#define MISC_END 0x0F
#define MISC_MOV 0x28 /* followed by two typed operand pairs */

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
#define OPERAND_D 0x86

/* Preset coils live in the misc group with a device nibble. */
#define OPCODE_OUT_T 0x06
#define OPCODE_OUT_C 0x0E
#define OPCODE_CONST 0x80

#define WORD_BLANK 0xFFFF

static uint16_t unknown_count;
static uint16_t last_bad_opcode;

static uint16_t fetch(uint32_t off) {
    return (uint16_t)(plc_program_read(off) | ((uint16_t)plc_program_read(off + 1) << 8));
}

/* Expands a device nibble and operand into a device number, applying the
 * 256-per-nibble extension for M and S. */
static uint16_t device_number(uint8_t dev, uint8_t n) {
    if (dev >= DEV_M && dev <= 0xB) {
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
    if (dev >= DEV_M && dev <= 0xB) return plc_get_m(i);
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
    if (dev >= DEV_M && dev <= 0xB) plc_set_m(i, v);
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
            if (operand == MISC_END) {
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
                    plc_set_s((uint16_t)(v & 0xFF), true);
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

        /* STL activates a step: the rung runs only while its state is on. */
        if (opcode == OPCODE_STL) {
            result = plc_get_s(operand);
            continue;
        }
        if (opcode == OPCODE_RET) {
            result = false;
            continue;
        }

        if (opcode == OPCODE_CONST) {
            continue; /* stray constant - already consumed by its instruction */
        }

        switch (op) {
            case OP_LD:
                if (block_sp < 16) block[block_sp++] = result;
                result = read_bit(dev, operand);
                break;
            case OP_LDI:
                if (block_sp < 16) block[block_sp++] = result;
                result = !read_bit(dev, operand);
                break;
            case OP_AND: result = result && read_bit(dev, operand); break;
            case OP_ANI: result = result && !read_bit(dev, operand); break;
            case OP_OR: result = result || read_bit(dev, operand); break;
            case OP_ORI: result = result || !read_bit(dev, operand); break;

            case OP_OUT: write_bit(dev, operand, result); break;

            case OP_SET:
                if (result) write_bit(dev, operand, true);
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
}
