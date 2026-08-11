#include "plc_storage.h"

#include <stdio.h>
#include <string.h>

#include "fx_protocol.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"
#include "plc_program.h"
#include "wifi_config.h"

/*
 * Reserved region at the very end of flash, far from the program image. One
 * 256-byte header page followed by the program buffer, totalling exactly eight
 * 4096-byte sectors.
 */
#define STORAGE_HEADER_BYTES 256
#define STORAGE_TOTAL_BYTES (STORAGE_HEADER_BYTES + PLC_PROGRAM_STORE_BYTES)
#define STORAGE_OFFSET (PICO_FLASH_SIZE_BYTES - STORAGE_TOTAL_BYTES)

#define STORAGE_MAGIC 0x32584649u /* "IFX2" */
#define STORAGE_WIFI_MAGIC 0x49464957u /* "WIFI" */

/*
 * The Wi-Fi credentials share the program's header page rather than getting a
 * sector of their own - it had 244 of its 256 bytes spare, and a separate
 * sector would have moved the storage region.
 *
 * They carry their own magic because they are appended: a device holding an
 * image written before this existed reads them as erased flash, which the
 * magic rejects as "not provisioned" instead of a 0xFF SSID.
 */
typedef struct {
    uint32_t magic;
    uint32_t length;
    uint32_t checksum;
    uint32_t wifi_magic;
    char wifi_ssid[WIFI_SSID_SIZE];
    char wifi_key[WIFI_KEY_SIZE];
} storage_header_t;

static bool dirty = false;
static uint32_t dirty_at_ms = 0;

/* RAM copy of the credentials, since a commit rebuilds the header page from
 * scratch and would otherwise erase them. */
static bool wifi_present = false;
static char wifi_ssid[WIFI_SSID_SIZE];
static char wifi_key[WIFI_KEY_SIZE];

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

    /* Independent of the program: a trainer can be provisioned before it has
     * ever been programmed, and must not lose its credentials to a bad or
     * absent program image. */
    if (h->wifi_magic == STORAGE_WIFI_MAGIC) {
        memcpy(wifi_ssid, h->wifi_ssid, sizeof wifi_ssid);
        memcpy(wifi_key, h->wifi_key, sizeof wifi_key);
        wifi_ssid[sizeof wifi_ssid - 1] = '\0';
        wifi_key[sizeof wifi_key - 1] = '\0';
        wifi_present = wifi_ssid[0] != '\0';
    }

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

void plc_storage_wifi_set(const char *ssid, const char *key) {
    snprintf(wifi_ssid, sizeof wifi_ssid, "%s", ssid);
    snprintf(wifi_key, sizeof wifi_key, "%s", key);
    wifi_present = true;
    plc_storage_mark_dirty();
}

bool plc_storage_wifi_get(const char **ssid, const char **key) {
    if (!wifi_present) {
        return false;
    }
    *ssid = wifi_ssid;
    *key = wifi_key;
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

    if (wifi_present) {
        h->wifi_magic = STORAGE_WIFI_MAGIC;
        memcpy(h->wifi_ssid, wifi_ssid, sizeof h->wifi_ssid);
        memcpy(h->wifi_key, wifi_key, sizeof h->wifi_key);
    }

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
