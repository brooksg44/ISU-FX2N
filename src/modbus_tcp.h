/*
 * modbus_tcp.h - Modbus TCP slave over the Pico W's Wi-Fi.
 *
 * Speaks the same device map as the RTU port on GPIO0/1, because both hand
 * their requests to modbus_pdu_exec(). Only the framing differs: a length
 * field and a transaction id instead of a checksum and a silent gap.
 *
 * There is no authentication - Modbus TCP has none to offer. Anything that can
 * reach port 502 can write any coil or register, which on a trainer means it
 * can turn on outputs. Put these on a lab network, not a public one.
 */
#ifndef MODBUS_TCP_H
#define MODBUS_TCP_H

#include <stdbool.h>
#include <stdint.h>

#define MODBUS_TCP_PORT 502

/* Starts listening. Requires lwIP to be up, so call it after net_init().
 * Returns false if the listener could not be bound. */
bool modbus_tcp_init(void);

/* Counters for the diagnostic dump. */
uint32_t modbus_tcp_connections(void);
uint32_t modbus_tcp_requests(void);

#endif /* MODBUS_TCP_H */
