#include "plc_memory.h"

#include <string.h>

plc_memory_t plc_mem;

void plc_memory_init(void) { memset(&plc_mem, 0, sizeof(plc_mem)); }

/* Shared bit helpers. Keeping the range check in one place is what makes the
 * per-device accessors safe to call with untrusted indices. */
static bool bit_get(const uint16_t *words, uint16_t count, uint16_t i) {
    if (i >= count) {
        return false;
    }
    return (words[i / 16] >> (i % 16)) & 1u;
}

static void bit_set(uint16_t *words, uint16_t count, uint16_t i, bool v) {
    if (i >= count) {
        return;
    }
    if (v) {
        words[i / 16] |= (uint16_t)(1u << (i % 16));
    } else {
        words[i / 16] &= (uint16_t)~(1u << (i % 16));
    }
}

bool plc_get_x(uint16_t i) { return bit_get(plc_mem.x, PLC_NUM_X, i); }
void plc_set_x(uint16_t i, bool v) { bit_set(plc_mem.x, PLC_NUM_X, i, v); }

bool plc_get_y(uint16_t i) { return bit_get(plc_mem.y, PLC_NUM_Y, i); }
void plc_set_y(uint16_t i, bool v) { bit_set(plc_mem.y, PLC_NUM_Y, i, v); }

bool plc_get_s(uint16_t i) { return bit_get(plc_mem.s, PLC_NUM_S, i); }
void plc_set_s(uint16_t i, bool v) { bit_set(plc_mem.s, PLC_NUM_S, i, v); }

bool plc_get_m(uint16_t i) {
    if (i >= PLC_M_SPECIAL_BASE) {
        return bit_get(plc_mem.m_special, PLC_NUM_M_SPECIAL, i - PLC_M_SPECIAL_BASE);
    }
    return bit_get(plc_mem.m, PLC_NUM_M, i);
}

void plc_set_m(uint16_t i, bool v) {
    if (i >= PLC_M_SPECIAL_BASE) {
        bit_set(plc_mem.m_special, PLC_NUM_M_SPECIAL, i - PLC_M_SPECIAL_BASE, v);
        return;
    }
    bit_set(plc_mem.m, PLC_NUM_M, i, v);
}

uint16_t plc_get_d(uint16_t i) {
    if (i >= PLC_D_SPECIAL_BASE) {
        uint16_t k = i - PLC_D_SPECIAL_BASE;
        return k < PLC_NUM_D_SPECIAL ? plc_mem.d_special[k] : 0;
    }
    return i < PLC_NUM_D ? plc_mem.d[i] : 0;
}

void plc_set_d(uint16_t i, uint16_t v) {
    if (i >= PLC_D_SPECIAL_BASE) {
        uint16_t k = i - PLC_D_SPECIAL_BASE;
        if (k < PLC_NUM_D_SPECIAL) {
            plc_mem.d_special[k] = v;
        }
        return;
    }
    if (i < PLC_NUM_D) {
        plc_mem.d[i] = v;
    }
}

uint32_t plc_get_d32(uint16_t i) {
    return (uint32_t)plc_get_d(i) | ((uint32_t)plc_get_d((uint16_t)(i + 1)) << 16);
}

void plc_set_d32(uint16_t i, uint32_t v) {
    plc_set_d(i, (uint16_t)(v & 0xFFFF));
    plc_set_d((uint16_t)(i + 1), (uint16_t)(v >> 16));
}
