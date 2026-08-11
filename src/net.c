#include "net.h"

#include <stdio.h>
#include <string.h>

#include "lwip/netif.h"
#include "modbus_tcp.h"
#include "pico/cyw43_arch.h"
#include "plc_storage.h"

static net_state_t state = NET_DISABLED;
static char address[16] = "none";

void net_init(void) {
    const char *ssid, *key;
    if (!plc_storage_wifi_get(&ssid, &key)) {
        state = NET_DISABLED;
        return;
    }

    if (cyw43_arch_init() != 0) {
        state = NET_FAILED;
        return;
    }
    cyw43_arch_enable_sta_mode();

    /* Asynchronous: an access point can take seconds to answer, and the scan
     * loop must not stop for it. */
    if (cyw43_arch_wifi_connect_async(ssid, key, CYW43_AUTH_WPA2_AES_PSK) != 0) {
        state = NET_FAILED;
        return;
    }
    state = NET_JOINING;
}

void net_task(void) {
    if (state == NET_DISABLED || state == NET_FAILED) {
        return;
    }

    /* Poll mode: lwIP and the radio only make progress from here. */
    cyw43_arch_poll();

    if (state == NET_UP) {
        return;
    }

    int link = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
    if (link == CYW43_LINK_UP) {
        const ip4_addr_t *ip = netif_ip4_addr(netif_default);
        snprintf(address, sizeof address, "%s", ip4addr_ntoa(ip));
        state = modbus_tcp_init() ? NET_UP : NET_FAILED;
    } else if (link < 0) {
        /* Negative status is a terminal join failure - a wrong passphrase or
         * an access point that refused us. Retrying on a loop would hammer the
         * radio, so it is reported and left alone until the next power cycle. */
        state = NET_FAILED;
    }
}

net_state_t net_state(void) { return state; }

const char *net_address(void) { return address; }
