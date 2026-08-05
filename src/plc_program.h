/*
 * plc_program.h - user program storage.
 *
 * GX Works 2 downloads the compiled FX instruction list into program memory,
 * which the protocol reaches at byte address 0x8000 via the extended 'E'
 * command (observed: E 01 8000 40 = read 64 bytes from program address 0).
 *
 * An FX2N holds 8000 program steps of 16 bits each, so 16000 bytes. Note that
 * "step" and "instruction" are not the same thing: a basic instruction is one
 * step, but applied instructions occupy several.
 *
 * This is plain RAM for now. Persisting it to flash so a program survives a
 * power cycle is the next milestone; keeping the buffer behind these
 * accessors means that change will not touch the protocol layer.
 */
#ifndef PLC_PROGRAM_H
#define PLC_PROGRAM_H

#include <stdint.h>

#define PLC_PROGRAM_STEPS 8000
#define PLC_PROGRAM_BYTES (PLC_PROGRAM_STEPS * 2)

/*
 * The PLC parameter block sits at the start of program memory. GX Works 2
 * reads exactly 92 bytes of it (0x8000 for 64, then 0x8040 for 28) before it
 * will allow a download, and refuses with "Data exceeds capacity of the
 * memory cassette" if the contents are implausible.
 *
 * The layout is not published in any source available to this project. What
 * matters here is that an erased 0xFF block declares maximum-sized comment
 * and file-register areas, which consume the whole 8000 steps - so the
 * parameter area is cleared to zero (no comment area, no file registers)
 * while the program area keeps the erased 0xFF convention.
 */
#define PLC_PROGRAM_PARAM_BYTES 256

/*
 * The buffer is rounded up to a whole number of 256-byte flash pages so it can
 * be written to flash in one operation. Only PLC_PROGRAM_BYTES of it is
 * addressable as program; the tail is padding.
 */
#define PLC_PROGRAM_STORE_BYTES 16128 /* 63 pages, >= PLC_PROGRAM_BYTES */

void plc_program_init(void);

/* Raw buffer, for the flash storage layer. Not for general use. */
uint8_t *plc_program_raw(void);

/* Byte access, indexed from 0 (protocol address 0x8000). Out-of-range reads
 * return 0xFF - erased-flash convention, and what an FX returns past the end
 * of the program - and out-of-range writes are ignored. */
uint8_t plc_program_read(uint32_t offset);
void plc_program_write(uint32_t offset, uint8_t value);

/* One 16-bit program step, low byte first. */
uint16_t plc_program_step(uint16_t step);

#endif /* PLC_PROGRAM_H */
