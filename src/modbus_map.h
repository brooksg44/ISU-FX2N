/*
 * modbus_map.h - Modbus address space to FX2N device mapping.
 *
 * A real FX2N has no Modbus at all - Mitsubishi added it with the FX3U's
 * ADPRW instruction, and even there the slave mapping is set by parameters
 * rather than fixed. So the layout below is this project's own convention,
 * chosen to be obvious to a student reading it: each Modbus table holds the
 * FX devices that behave the same way that table does.
 *
 *   Coils (FC 01/05/15) - readable and writable bits
 *       0 -  255   Y0-Y255      outputs
 *    1000 - 4071   M0-M3071     auxiliary relays
 *    5000 - 5999   S0-S999      state relays
 *
 *   Discrete inputs (FC 02) - read-only bits
 *       0 -  255   X0-X255      inputs
 *    1000 - 1255   T0-T255      timer contacts
 *    2000 - 2255   C0-C255      counter contacts
 *
 *   Holding registers (FC 03/06/16) - readable and writable words
 *       0 - 7999   D0-D7999     data registers
 *    8000 - 8255   D8000-D8255  special registers
 *
 *   Input registers (FC 04) - read-only words
 *       0 -  255   T0-T255      timer current values
 *    1000 - 1255   C0-C255      counter current values
 *    2000 - 2002   AI0-AI2      analog inputs, raw 0-4095
 *
 * Note that X and Y are numbered in OCTAL on the ladder side: Modbus coil 8
 * is Y10 in GX Works, not Y8. The mapping is by device index, so the octal
 * notation is purely how the number is written.
 *
 * Every function returns false for an address outside these ranges, which the
 * protocol layer turns into a Modbus "illegal data address" exception rather
 * than silently reading zero.
 */
#ifndef MODBUS_MAP_H
#define MODBUS_MAP_H

#include <stdbool.h>
#include <stdint.h>

#define MB_COIL_Y 0
#define MB_COIL_M 1000
#define MB_COIL_S 5000

#define MB_DISCRETE_X 0
#define MB_DISCRETE_T 1000
#define MB_DISCRETE_C 2000

#define MB_HOLDING_D 0
#define MB_HOLDING_D_SPECIAL 8000

#define MB_INPUT_T 0
#define MB_INPUT_C 1000
#define MB_INPUT_ANALOG 2000

bool modbus_read_coil(uint16_t addr, bool *out);
bool modbus_write_coil(uint16_t addr, bool value);
bool modbus_read_discrete(uint16_t addr, bool *out);
bool modbus_read_holding(uint16_t addr, uint16_t *out);
bool modbus_write_holding(uint16_t addr, uint16_t value);
bool modbus_read_input(uint16_t addr, uint16_t *out);

#endif /* MODBUS_MAP_H */
