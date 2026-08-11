/*
 * net.h - Wi-Fi bring-up for the Modbus TCP slave.
 *
 * Credentials come from flash, not from the firmware image - see wifi_config.h
 * for why. A trainer that has never been provisioned simply never starts the
 * radio, which is also what keeps the published UF2 useful to someone who has
 * no interest in networking.
 *
 * The join is asynchronous. A PLC that stopped scanning while it waited for an
 * access point would be a poor controller, so net_task() reports progress and
 * the scan loop keeps running throughout.
 */
#ifndef NET_H
#define NET_H

#include <stdbool.h>

typedef enum {
    NET_DISABLED, /* no credentials stored */
    NET_FAILED,   /* radio or join failed */
    NET_JOINING,
    NET_UP,
} net_state_t;

/* Starts the radio and the join, if credentials are stored. */
void net_init(void);

/* Services lwIP and advances the join. Call once per scan. */
void net_task(void);

net_state_t net_state(void);

/* Dotted-quad address once NET_UP, otherwise "none". */
const char *net_address(void);

#endif /* NET_H */
