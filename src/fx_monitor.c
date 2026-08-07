#include "fx_monitor.h"

#include <string.h>

#include "fx_addr.h"

static uint8_t list[FX_MON_LIST_BYTES];
static uint8_t result[FX_MON_RESULT_BYTES];
static uint16_t result_len;

static uint16_t le16(const uint8_t *p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

bool fx_monitor_owns_write(uint16_t addr) {
    return addr >= FX_MON_LIST && addr < FX_MON_RESULT;
}

bool fx_monitor_owns_read(uint16_t addr) {
    return addr >= FX_MON_RESULT && addr - FX_MON_RESULT < result_len;
}

void fx_monitor_write(uint16_t addr, uint8_t value) {
    list[addr - FX_MON_LIST] = value;
}

uint8_t fx_monitor_read(uint16_t addr) {
    return result[addr - FX_MON_RESULT];
}

uint16_t fx_monitor_result_len(void) { return result_len; }

void fx_monitor_sample(void) {
    uint16_t word_count = list[0];
    uint16_t bit_count = le16(&list[2]);
    uint32_t list_len = 4u + 2u * word_count + 2u * bit_count;
    uint32_t sample_len = 2u * word_count + (bit_count + 7u) / 8u;

    if (list[1] != 0x81 || list_len > sizeof list || sample_len > sizeof result) {
        result_len = 0;
        return;
    }

    result_len = (uint16_t)sample_len;
    memset(result, 0, result_len);

    for (uint16_t i = 0; i < word_count; i++) {
        uint16_t addr = le16(&list[4 + 2 * i]);
        result[2 * i] = fx_addr_read_byte(addr);
        result[2 * i + 1] = fx_addr_read_byte((uint16_t)(addr + 1));
    }

    const uint8_t *bits = &list[4 + 2 * word_count];
    for (uint16_t i = 0; i < bit_count; i++) {
        if (fx_addr_force_read(le16(&bits[2 * i]))) {
            result[2 * word_count + i / 8] |= (uint8_t)(1u << (i % 8));
        }
    }
}
