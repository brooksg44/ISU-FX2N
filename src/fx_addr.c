#include "fx_addr.h"

#include "plc_memory.h"
#include "plc_program.h"

bool fx_addr_read_bit(uint16_t addr, uint8_t bit) {
    if (addr < FX_ADDR_X) {
        return plc_get_s((uint16_t)((addr - FX_ADDR_S) * 8 + bit));
    }
    if (addr < FX_ADDR_Y) {
        return plc_get_x((uint16_t)((addr - FX_ADDR_X) * 8 + bit));
    }
    if (addr < FX_ADDR_T_CONTACT) {
        return plc_get_y((uint16_t)((addr - FX_ADDR_Y) * 8 + bit));
    }
    if (addr < 0x00E0) {
        uint16_t i = (uint16_t)((addr - FX_ADDR_T_CONTACT) * 8 + bit);
        return i < PLC_NUM_T ? plc_mem.t[i].done : false;
    }
    if (addr < FX_ADDR_M) {
        return false; /* 0x00E0-0x00FF is not assigned */
    }
    if (addr < FX_ADDR_C_CONTACT) {
        return plc_get_m((uint16_t)((addr - FX_ADDR_M) * 8 + bit));
    }
    if (addr < FX_ADDR_M_SPECIAL) {
        uint16_t i = (uint16_t)((addr - FX_ADDR_C_CONTACT) * 8 + bit);
        return i < PLC_NUM_C ? plc_mem.c[i].done : false;
    }
    if (addr < FX_ADDR_BIT_END) {
        uint16_t i = (uint16_t)((addr - FX_ADDR_M_SPECIAL) * 8 + bit);
        return plc_get_m((uint16_t)(PLC_M_SPECIAL_BASE + i));
    }
    return false;
}

void fx_addr_write_bit(uint16_t addr, uint8_t bit, bool v) {
    if (addr < FX_ADDR_X) {
        plc_set_s((uint16_t)((addr - FX_ADDR_S) * 8 + bit), v);
    } else if (addr < FX_ADDR_Y) {
        plc_set_x((uint16_t)((addr - FX_ADDR_X) * 8 + bit), v);
    } else if (addr < FX_ADDR_T_CONTACT) {
        plc_set_y((uint16_t)((addr - FX_ADDR_Y) * 8 + bit), v);
    } else if (addr < 0x00E0) {
        uint16_t i = (uint16_t)((addr - FX_ADDR_T_CONTACT) * 8 + bit);
        if (i < PLC_NUM_T) plc_mem.t[i].done = v;
    } else if (addr < FX_ADDR_M) {
        /* not assigned */
    } else if (addr < FX_ADDR_C_CONTACT) {
        plc_set_m((uint16_t)((addr - FX_ADDR_M) * 8 + bit), v);
    } else if (addr < FX_ADDR_M_SPECIAL) {
        uint16_t i = (uint16_t)((addr - FX_ADDR_C_CONTACT) * 8 + bit);
        if (i < PLC_NUM_C) plc_mem.c[i].done = v;
    } else if (addr < FX_ADDR_BIT_END) {
        uint16_t i = (uint16_t)((addr - FX_ADDR_M_SPECIAL) * 8 + bit);
        plc_set_m((uint16_t)(PLC_M_SPECIAL_BASE + i), v);
    }
}

/* Word devices are little-endian across their two byte addresses. */
static uint8_t word_byte(uint16_t value, uint16_t addr) {
    return (addr & 1u) ? (uint8_t)(value >> 8) : (uint8_t)(value & 0xFF);
}

static uint16_t word_merge(uint16_t old, uint16_t addr, uint8_t b) {
    return (addr & 1u) ? (uint16_t)((old & 0x00FF) | ((uint16_t)b << 8))
                       : (uint16_t)((old & 0xFF00) | b);
}

uint8_t fx_addr_read_byte(uint16_t addr) {
    if (addr < FX_ADDR_BIT_END) {
        uint8_t byte = 0;
        for (uint8_t b = 0; b < 8; b++) {
            if (fx_addr_read_bit(addr, b)) {
                byte |= (uint8_t)(1u << b);
            }
        }
        return byte;
    }
    if (addr >= FX_ADDR_T_CURRENT && addr < FX_ADDR_C_CURRENT) {
        uint16_t i = (uint16_t)((addr - FX_ADDR_T_CURRENT) / 2);
        return i < PLC_NUM_T ? word_byte(plc_mem.t[i].current, addr) : 0;
    }
    if (addr >= FX_ADDR_C_CURRENT && addr < FX_ADDR_C_CURRENT32) {
        uint16_t i = (uint16_t)((addr - FX_ADDR_C_CURRENT) / 2);
        return i < 200 ? word_byte((uint16_t)plc_mem.c[i].current, addr) : 0;
    }
    if (addr >= FX_ADDR_C_CURRENT32 && addr < FX_ADDR_D_SPECIAL) {
        uint16_t off = (uint16_t)(addr - FX_ADDR_C_CURRENT32);
        uint16_t i = (uint16_t)(200 + off / 4);
        if (i >= PLC_NUM_C) return 0;
        return (uint8_t)(((uint32_t)plc_mem.c[i].current >> (8 * (off & 3))) & 0xFF);
    }
    if (addr >= FX_ADDR_D_SPECIAL && addr < FX_ADDR_D) {
        uint16_t i = (uint16_t)((addr - FX_ADDR_D_SPECIAL) / 2);
        return word_byte(plc_get_d((uint16_t)(PLC_D_SPECIAL_BASE + i)), addr);
    }
    if (addr >= FX_ADDR_PROGRAM) {
        return plc_program_read((uint32_t)(addr - FX_ADDR_PROGRAM));
    }
    if (addr >= FX_ADDR_D && addr < FX_ADDR_D_END) {
        uint16_t i = (uint16_t)((addr - FX_ADDR_D) / 2);
        return word_byte(plc_get_d(i), addr);
    }
    return 0;
}

void fx_addr_write_byte(uint16_t addr, uint8_t value) {
    if (addr < FX_ADDR_BIT_END) {
        for (uint8_t b = 0; b < 8; b++) {
            fx_addr_write_bit(addr, b, (value >> b) & 1u);
        }
        return;
    }
    if (addr >= FX_ADDR_T_CURRENT && addr < FX_ADDR_C_CURRENT) {
        uint16_t i = (uint16_t)((addr - FX_ADDR_T_CURRENT) / 2);
        if (i < PLC_NUM_T) {
            plc_mem.t[i].current = word_merge(plc_mem.t[i].current, addr, value);
        }
        return;
    }
    if (addr >= FX_ADDR_C_CURRENT && addr < FX_ADDR_C_CURRENT32) {
        uint16_t i = (uint16_t)((addr - FX_ADDR_C_CURRENT) / 2);
        if (i < 200) {
            plc_mem.c[i].current =
                (int32_t)word_merge((uint16_t)plc_mem.c[i].current, addr, value);
        }
        return;
    }
    if (addr >= FX_ADDR_D_SPECIAL && addr < FX_ADDR_D) {
        uint16_t i = (uint16_t)(PLC_D_SPECIAL_BASE + (addr - FX_ADDR_D_SPECIAL) / 2);
        plc_set_d(i, word_merge(plc_get_d(i), addr, value));
        return;
    }
    if (addr >= FX_ADDR_PROGRAM) {
        plc_program_write((uint32_t)(addr - FX_ADDR_PROGRAM), value);
        return;
    }
    if (addr >= FX_ADDR_D && addr < FX_ADDR_D_END) {
        uint16_t i = (uint16_t)((addr - FX_ADDR_D) / 2);
        plc_set_d(i, word_merge(plc_get_d(i), addr, value));
    }
}

bool fx_addr_force_known(uint16_t a) {
    return a < FX_FORCE_Y_BASE + PLC_NUM_Y ||
           (a >= FX_FORCE_MS_BASE && a < FX_FORCE_MS_BASE + PLC_NUM_M_SPECIAL) ||
           (a >= FX_FORCE_X_BASE && a < FX_FORCE_X_BASE + PLC_NUM_X);
}

void fx_addr_force(uint16_t a, bool on) {
    if (a < FX_FORCE_Y_BASE) {
        plc_set_m((uint16_t)(a - FX_FORCE_M_BASE), on);
    } else if (a < FX_FORCE_Y_BASE + PLC_NUM_Y) {
        plc_set_y((uint16_t)(a - FX_FORCE_Y_BASE), on);
    } else if (a >= FX_FORCE_MS_BASE && a < FX_FORCE_MS_BASE + PLC_NUM_M_SPECIAL) {
        plc_set_m((uint16_t)(PLC_M_SPECIAL_BASE + (a - FX_FORCE_MS_BASE)), on);
    } else if (a >= FX_FORCE_X_BASE && a < FX_FORCE_X_BASE + PLC_NUM_X) {
        plc_set_x((uint16_t)(a - FX_FORCE_X_BASE), on);
    }
    /* Anything else is in a region we have not placed - ignored on purpose. */
}
