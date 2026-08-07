#ifndef FX_MONITOR_H
#define FX_MONITOR_H

#include <stdbool.h>
#include <stdint.h>

#define FX_MON_LIST 0x1400u
#define FX_MON_RESULT 0x1790u
#define FX_MON_LIST_BYTES (FX_MON_RESULT - FX_MON_LIST)
#define FX_MON_RESULT_BYTES 128u

bool fx_monitor_owns_write(uint16_t addr);
bool fx_monitor_owns_read(uint16_t addr);
void fx_monitor_write(uint16_t addr, uint8_t value);
uint8_t fx_monitor_read(uint16_t addr);
void fx_monitor_sample(void);
uint16_t fx_monitor_result_len(void);

#endif
