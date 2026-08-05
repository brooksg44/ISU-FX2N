/*
 * modbus.h - Modbus RTU slave on the trainer's UART pins.
 *
 * GPIO0 = TX (pin 1), GPIO1 = RX (pin 2), per FX1N-Trainer-Pins.pdf. This is
 * the port the GX Works protocol does NOT use - programming runs over USB CDC,
 * which leaves this UART free, mirroring the two-port arrangement of the
 * STM32 design this project came from.
 *
 * Framing is standard Modbus RTU: a frame ends after 3.5 character times of
 * silence, and is validated with the CRC-16 used by Modbus (polynomial 0xA001).
 *
 * Supported functions: 01 read coils, 02 read discrete inputs, 03 read holding
 * registers, 04 read input registers, 05 write single coil, 06 write single
 * register, 15 write multiple coils, 16 write multiple registers.
 *
 * See modbus_map.h for which FX devices each address range reaches.
 */
#ifndef MODBUS_H
#define MODBUS_H

#include <stdbool.h>
#include <stdint.h>

#define MODBUS_DEFAULT_SLAVE 1
#define MODBUS_DEFAULT_BAUD 9600

void modbus_init(uint8_t slave_address, uint32_t baud);

/* Services the port. Call once per scan; never blocks. */
void modbus_task(void);

/* Diagnostics, mirroring the GX Works side so both ports can be compared. */
uint32_t modbus_rx_frames(void);
uint32_t modbus_bad_frames(void);

#endif /* MODBUS_H */
