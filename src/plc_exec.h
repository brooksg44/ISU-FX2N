/*
 * plc_exec.h - the ladder interpreter.
 *
 * Executes the FX instruction words that GX Works 2 downloads. The encoding
 * below was derived by downloading known programs and reading back program
 * memory; it is not published by Mitsubishi in any source available to this
 * project, so treat the table in plc_exec.c as the authority.
 *
 * Each step is one 16-bit little-endian word stored as [operand][opcode]:
 *
 *   opcode high nibble = operation   2 LD  3 LDI  4 AND  5 ANI  6 OR
 *                                    C OUT D SET  E RST  8 PLS  0 misc
 *   opcode low  nibble = device      4 X   5 Y    6 T    8 M    E C
 *                                    F special M (operand = number - 8000)
 *   operand            = device number
 *
 * Multi-word instructions, matching the documented FX step counts:
 *   OUT T / OUT C   3 steps: coil word then a 32-bit constant in two 0x80 words
 *   PLS             2 steps: a 0x0008 prefix word then the device word
 *   PLF             2 steps: as PLS but with a 0x0009 prefix - inferred from
 *                            the basic-instruction ordering, not captured
 *   END             1 step:  opcode 0x00, operand 0x0F
 *
 * 0xFFFF is unwritten memory. GX Works leaves gaps between download blocks, so
 * it must be skipped as a NOP rather than treated as the end of the program.
 */
#ifndef PLC_EXEC_H
#define PLC_EXEC_H

#include <stdbool.h>
#include <stdint.h>

/* Program code begins immediately after the 92-byte parameter block. */
#define PLC_CODE_OFFSET 0x5C

/* Runs one scan of the downloaded program. Does nothing if no valid program
 * is present. Safe to call every scan. */
void plc_exec_scan(void);

/* True if program memory holds something that looks like a program, i.e. an
 * END instruction is reachable. */
bool plc_exec_has_program(void);

/* Count of instruction words the last scan could not decode. Non-zero means
 * the program uses something not yet implemented; the offending opcode is
 * reported by plc_exec_last_bad_opcode(). */
uint16_t plc_exec_unknown_count(void);
uint16_t plc_exec_last_bad_opcode(void);

#endif /* PLC_EXEC_H */
