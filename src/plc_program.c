#include "plc_program.h"

#include <string.h>

static uint8_t program[PLC_PROGRAM_STORE_BYTES];

/*
 * Default parameter block, captured verbatim from what GX Works 2 downloads
 * to an FX2N. Having a valid block at power-on means a device that has never
 * been programmed still answers the pre-download checks sensibly instead of
 * looking corrupt.
 *
 *   +00  memory capacity, 8 = 8K steps
 *   +02  comment blocks and file register blocks, both none
 *   +08  program title, 32 ASCII spaces (blank)
 *   +30  latched device range boundaries
 */
static const uint8_t default_parameters[92] = {
    0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x20, 0x20, 0x20, 0x20,
    0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
    0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
    0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0xF4, 0x09, 0xFF, 0x0B,
    0xF4, 0x01, 0xE7, 0x03, 0x64, 0x0E, 0xC7, 0x0E, 0xDC, 0x0E, 0xFF, 0x0E,
    /* +40, from the second parameter frame (E 11 8040 1C) */
    0x90, 0x01, 0xFE, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00,
};

uint8_t *plc_program_raw(void) { return program; }

void plc_program_init(void) {
    /* 0xFF is the erased state, and is what an FX reads past the end of a
     * program. The interpreter treats it as "no program". */
    memset(program, 0xFF, sizeof(program));

    /* ...except the parameter block, where 0xFF reads as maximum-sized
     * comment and file-register areas and makes GX Works refuse to download.
     * See the note in plc_program.h. */
    memset(program, 0x00, PLC_PROGRAM_PARAM_BYTES);
    memcpy(program, default_parameters, sizeof(default_parameters));
}

uint8_t plc_program_read(uint32_t offset) {
    return offset < PLC_PROGRAM_BYTES ? program[offset] : 0xFF;
}

void plc_program_write(uint32_t offset, uint8_t value) {
    if (offset < PLC_PROGRAM_BYTES) {
        program[offset] = value;
    }
}

uint16_t plc_program_step(uint16_t step) {
    uint32_t o = (uint32_t)step * 2;
    return (uint16_t)(plc_program_read(o) | ((uint16_t)plc_program_read(o + 1) << 8));
}
