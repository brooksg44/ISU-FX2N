/*
 * fx_addr.h - device <-> protocol address mapping for the FX programming port.
 *
 * Deliberately free of any Pico SDK dependency so it can be compiled and
 * tested on the host (see tests/). This is the layer most likely to contain a
 * silent bug: an off-by-one here produces a PLC that talks to GX Works 2
 * happily but reports the wrong device.
 *
 * Protocol addresses are BYTE addresses into the device image. Bit devices
 * pack 8 devices per byte; word devices take 2 bytes, low byte first.
 *
 *   0x0000  S       bit    0x0800  T current values   word
 *   0x0080  X       bit    0x0A00  C current (16-bit) word
 *   0x00A0  Y       bit    0x0C00  C current (32-bit) dword
 *   0x00C0  T con.  bit    0x0E00  D8000-D8255        word
 *   0x0100  M       bit    0x1000  D0-D7999           word
 *   0x01C0  C con.  bit
 *   0x01E0  M8000+  bit
 */
#ifndef FX_ADDR_H
#define FX_ADDR_H

#include <stdbool.h>
#include <stdint.h>

#define FX_ADDR_S 0x0000
#define FX_ADDR_X 0x0080
#define FX_ADDR_Y 0x00A0
#define FX_ADDR_T_CONTACT 0x00C0
#define FX_ADDR_M 0x0100
#define FX_ADDR_C_CONTACT 0x01C0
#define FX_ADDR_M_SPECIAL 0x01E0
#define FX_ADDR_BIT_END 0x0200
#define FX_ADDR_T_CURRENT 0x0800
#define FX_ADDR_C_CURRENT 0x0A00
#define FX_ADDR_C_CURRENT32 0x0C00
#define FX_ADDR_D_SPECIAL 0x0E00
#define FX_ADDR_D 0x1000
#define FX_ADDR_D_END 0x4E80 /* 0x1000 + 8000 registers * 2 bytes */

/* User program memory. Observed from GX Works: it reads 0x8000 with the
 * extended 'E' command before writing a program. */
#define FX_ADDR_PROGRAM 0x8000

uint8_t fx_addr_read_byte(uint16_t addr);
void fx_addr_write_byte(uint16_t addr, uint8_t value);

/* Single-bit access, used by the force on/off commands. */
bool fx_addr_read_bit(uint16_t addr, uint8_t bit);
void fx_addr_write_bit(uint16_t addr, uint8_t bit, bool v);

/*
 * Force commands use a different address space from reads and writes: a flat
 * device-number space, sent little-endian. Derived by forcing devices whose
 * identity was known:
 *
 *   Force Y0   -> 0x0C00 = 3072
 *   Force M300 -> 0x012C = 300
 *
 * The full layout was then recovered from the watch lists GX Works writes in
 * Monitor Mode, which name devices in this same space:
 *
 *   0x0000  M0..M3071      0x012C seen for M300
 *   0x0C00  Y0..Y255       matched the coils of a downloaded program
 *   0x0E00  M8000..M8255   0x0E3D..0x0E42 = M8061, M8064-M8066, the error
 *                          flags GX Works monitors unconditionally
 *   0x1200  X0..X255       matched the contacts of a downloaded program
 *
 * S, T and C have not been placed yet; addresses outside the ranges below are
 * ignored rather than written somewhere guessed.
 */
#define FX_FORCE_M_BASE 0
#define FX_FORCE_Y_BASE 3072
#define FX_FORCE_MS_BASE 3584
#define FX_FORCE_X_BASE 4608
#define FX_FORCE_END 4864

void fx_addr_force(uint16_t force_addr, bool on);
bool fx_addr_force_known(uint16_t force_addr);

#endif /* FX_ADDR_H */
