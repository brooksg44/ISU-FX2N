/*
 * modbus_tcp.h - Modbus TCP slave over the Pico W's Wi-Fi.
 *
 * Serves the device map in modbus_map.h, handing each request to
 * modbus_pdu_exec(). That split predates this file: request handling was
 * separated from an RTU port that has since been removed, and it stays split
 * because it is the half that can be tested on the host.
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
