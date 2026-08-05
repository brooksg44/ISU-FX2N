#include "modbus_map.h"

#include "plc_memory.h"

/* Helper: is addr inside [base, base+count)? If so, yields the index. */
static bool in_range(uint16_t addr, uint16_t base, uint16_t count, uint16_t *index) {
    if (addr < base || addr >= (uint16_t)(base + count)) {
        return false;
    }
    *index = (uint16_t)(addr - base);
    return true;
}

bool modbus_read_coil(uint16_t addr, bool *out) {
    uint16_t i;
    if (in_range(addr, MB_COIL_Y, PLC_NUM_Y, &i)) {
        *out = plc_get_y(i);
        return true;
    }
    if (in_range(addr, MB_COIL_M, PLC_NUM_M, &i)) {
        *out = plc_get_m(i);
        return true;
    }
    if (in_range(addr, MB_COIL_S, PLC_NUM_S, &i)) {
        *out = plc_get_s(i);
        return true;
    }
    return false;
}

bool modbus_write_coil(uint16_t addr, bool value) {
    uint16_t i;
    if (in_range(addr, MB_COIL_Y, PLC_NUM_Y, &i)) {
        plc_set_y(i, value);
        return true;
    }
    if (in_range(addr, MB_COIL_M, PLC_NUM_M, &i)) {
        plc_set_m(i, value);
        return true;
    }
    if (in_range(addr, MB_COIL_S, PLC_NUM_S, &i)) {
        plc_set_s(i, value);
        return true;
    }
    return false;
}

bool modbus_read_discrete(uint16_t addr, bool *out) {
    uint16_t i;
    if (in_range(addr, MB_DISCRETE_X, PLC_NUM_X, &i)) {
        *out = plc_get_x(i);
        return true;
    }
    if (in_range(addr, MB_DISCRETE_T, PLC_NUM_T, &i)) {
        *out = plc_mem.t[i].done;
        return true;
    }
    if (in_range(addr, MB_DISCRETE_C, PLC_NUM_C, &i)) {
        *out = plc_mem.c[i].done;
        return true;
    }
    return false;
}

bool modbus_read_holding(uint16_t addr, uint16_t *out) {
    uint16_t i;
    if (in_range(addr, MB_HOLDING_D, PLC_NUM_D, &i)) {
        *out = plc_get_d(i);
        return true;
    }
    if (in_range(addr, MB_HOLDING_D_SPECIAL, PLC_NUM_D_SPECIAL, &i)) {
        *out = plc_get_d((uint16_t)(PLC_D_SPECIAL_BASE + i));
        return true;
    }
    return false;
}

bool modbus_write_holding(uint16_t addr, uint16_t value) {
    uint16_t i;
    if (in_range(addr, MB_HOLDING_D, PLC_NUM_D, &i)) {
        plc_set_d(i, value);
        return true;
    }
    if (in_range(addr, MB_HOLDING_D_SPECIAL, PLC_NUM_D_SPECIAL, &i)) {
        plc_set_d((uint16_t)(PLC_D_SPECIAL_BASE + i), value);
        return true;
    }
    return false;
}

bool modbus_read_input(uint16_t addr, uint16_t *out) {
    uint16_t i;
    if (in_range(addr, MB_INPUT_T, PLC_NUM_T, &i)) {
        *out = plc_mem.t[i].current;
        return true;
    }
    if (in_range(addr, MB_INPUT_C, PLC_NUM_C, &i)) {
        /* 32-bit counters are reported truncated to their low word here; the
         * full value is available through the D-mapped special registers. */
        *out = (uint16_t)plc_mem.c[i].current;
        return true;
    }
    if (in_range(addr, MB_INPUT_ANALOG, 3, &i)) {
        *out = plc_get_d((uint16_t)(D_ANALOG_BASE + i));
        return true;
    }
    return false;
}
