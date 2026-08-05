/*
 * board.h - Hardware pin map for the ISU FX2N trainer (Raspberry Pi Pico WH).
 *
 * Authoritative source: FX1N-Trainer-Pins.pdf in the project root.
 *
 * Note the asymmetry: input pins ASCEND (I0=GPIO6 .. I9=GPIO15) but output
 * pins DESCEND (O0=GPIO22 .. O6=GPIO16). Both are looked up through tables
 * below rather than computed, so neither can be shifted into place.
 *
 * Polarity is active-HIGH on both banks. Per the trainer manual, pushbuttons
 * connect the 3.3 V bus to an input pin, and LED cathodes are grounded, so a
 * high pin means "on" in both directions. (The STM32 original this project is
 * derived from was inverted on both banks - do not copy its polarity.)
 */
#ifndef BOARD_H
#define BOARD_H

#include <stdbool.h>
#include <stdint.h>

#define BOARD_NUM_INPUTS 10 /* X0..X7, X10, X11 in FX octal numbering */
#define BOARD_NUM_OUTPUTS 7 /* Y0..Y6 */
#define BOARD_NUM_ANALOG 3  /* AI0..AI2 */

#define BOARD_STS0_GPIO 2 /* status LED 0 - RUN indicator */
#define BOARD_STS1_GPIO 3 /* status LED 1 - ERROR indicator */

/* Configures every trainer pin. Call once before anything else. */
void board_init(void);

/* Bit n = input n, 1 when the input is energised. */
uint16_t board_read_inputs(void);

/* Bit n = output n, 1 energises the output. Bits above BOARD_NUM_OUTPUTS
 * are ignored. */
void board_write_outputs(uint16_t bits);

/* Raw 12-bit ADC reading (0..4095) for AI0..AI2, or 0 if ch is out of range. */
uint16_t board_read_analog(uint8_t ch);

void board_status_led(uint8_t idx, bool on);

#endif /* BOARD_H */
