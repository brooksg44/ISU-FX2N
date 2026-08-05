/*
 * plc_storage.h - persisting the user program to on-board flash.
 *
 * The STM32 original used an external I2C EEPROM for this. The trainer has no
 * EEPROM and no pins for one, so the program is kept in a reserved region at
 * the end of the Pico's 2 MB flash instead.
 *
 * Writing flash on an RP2040 stalls execution - the core cannot fetch from
 * flash while a sector is being erased - so a commit is never done in the
 * middle of a download. Instead a write marks the program dirty and the commit
 * happens once the link has been quiet for a moment, which is also when a
 * download has finished.
 *
 * This file is deliberately the only one that knows about flash, so the rest
 * of the program logic stays portable and host-testable.
 */
#ifndef PLC_STORAGE_H
#define PLC_STORAGE_H

#include <stdbool.h>
#include <stdint.h>

/* Loads a saved program into program memory, if a valid one is present.
 * Returns true if a program was restored. */
bool plc_storage_load(void);

/* Records that program memory has changed and needs committing. */
void plc_storage_mark_dirty(void);

/* Commits a dirty program once the link has been idle. Call from the main
 * loop; it rate-limits itself and does nothing when clean. */
void plc_storage_task(void);

#endif /* PLC_STORAGE_H */
