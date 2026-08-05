#include "plc_storage.h"

#include <string.h>

#include "fx_protocol.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"
#include "plc_program.h"

/*
 * Reserved region at the very end of flash, far from the program image. One
 * 256-byte header page followed by the program buffer, totalling exactly four
 * 4096-byte sectors.
 */
#define STORAGE_HEADER_BYTES 256
#define STORAGE_TOTAL_BYTES (STORAGE_HEADER_BYTES + PLC_PROGRAM_STORE_BYTES)
#define STORAGE_OFFSET (PICO_FLASH_SIZE_BYTES - STORAGE_TOTAL_BYTES)

#define STORAGE_MAGIC 0x32584649u /* "IFX2" */

typedef struct {
    uint32_t magic;
    uint32_t length;
    uint32_t checksum;
} storage_header_t;

static bool dirty = false;
static uint32_t dirty_at_ms = 0;

/* Additive checksum - enough to reject a partially written or erased region,
 * and simple enough to read. */
static uint32_t checksum_of(const uint8_t *p, uint32_t n) {
    uint32_t sum = 0;
    for (uint32_t i = 0; i < n; i++) {
        sum += p[i];
    }
    return sum;
}

static const uint8_t *flash_base(void) {
    return (const uint8_t *)(XIP_BASE + STORAGE_OFFSET);
}

bool plc_storage_load(void) {
    const storage_header_t *h = (const storage_header_t *)flash_base();
    if (h->magic != STORAGE_MAGIC || h->length != PLC_PROGRAM_STORE_BYTES) {
        return false;
    }

    const uint8_t *data = flash_base() + STORAGE_HEADER_BYTES;
    if (checksum_of(data, h->length) != h->checksum) {
        return false;
    }

    memcpy(plc_program_raw(), data, PLC_PROGRAM_STORE_BYTES);
    return true;
}

void plc_storage_mark_dirty(void) {
    dirty = true;
    dirty_at_ms = to_ms_since_boot(get_absolute_time());
}

static void commit(void) {
    static uint8_t header_page[STORAGE_HEADER_BYTES];
    storage_header_t *h = (storage_header_t *)header_page;

    memset(header_page, 0xFF, sizeof(header_page));
    h->magic = STORAGE_MAGIC;
    h->length = PLC_PROGRAM_STORE_BYTES;
    h->checksum = checksum_of(plc_program_raw(), PLC_PROGRAM_STORE_BYTES);

    /* Interrupts must be off: the flash controller cannot serve code fetches
     * while erasing, and any ISR running from flash would fault. USB is
     * unresponsive for the duration, which is why this only runs when idle. */
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(STORAGE_OFFSET, STORAGE_TOTAL_BYTES);
    flash_range_program(STORAGE_OFFSET, header_page, STORAGE_HEADER_BYTES);
    flash_range_program(STORAGE_OFFSET + STORAGE_HEADER_BYTES, plc_program_raw(),
                        PLC_PROGRAM_STORE_BYTES);
    restore_interrupts(ints);

    dirty = false;
}

void plc_storage_task(void) {
    if (!dirty) {
        return;
    }

    /* Hold off until both the program has settled and the link is quiet, so a
     * commit never lands in the middle of a multi-frame download. */
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (now - dirty_at_ms < 2000) {
        return;
    }
    commit();
}
