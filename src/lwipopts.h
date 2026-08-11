/*
 * lwipopts.h - lwIP configuration for the Modbus TCP slave.
 *
 * NO_SYS mode with no threads: lwIP runs from cyw43_arch_poll() inside the PLC
 * scan loop, exactly where the GX Works link is already serviced. That keeps
 * the one rule this firmware is built around - devices
 * are read, the program runs against a snapshot, then communications happen -
 * and it avoids needing any locking around the device image.
 *
 * Sized for a handful of Modbus clients on a lab network, not for throughput.
 */
#ifndef LWIPOPTS_H
#define LWIPOPTS_H

#define NO_SYS 1
#define LWIP_SOCKET 0
#define LWIP_NETCONN 0

#define MEM_LIBC_MALLOC 0
#define MEM_ALIGNMENT 4
#define MEM_SIZE 4000
#define MEMP_NUM_TCP_SEG 32
#define MEMP_NUM_ARP_QUEUE 10
#define PBUF_POOL_SIZE 24

#define LWIP_ARP 1
#define LWIP_ETHERNET 1
#define LWIP_ICMP 1
#define LWIP_RAW 1
#define LWIP_NETIF_STATUS_CALLBACK 1
#define LWIP_NETIF_LINK_CALLBACK 1
#define LWIP_NETIF_HOSTNAME 1
#define LWIP_NETCONN_FULLDUPLEX 0

#define LWIP_IPV4 1
#define LWIP_TCP 1
#define LWIP_UDP 1
#define LWIP_DNS 1
#define LWIP_DHCP 1
#define DHCP_DOES_ARP_CHECK 0
#define LWIP_DHCP_DOES_ACD_CHECK 0

#define TCP_WND (8 * TCP_MSS)
#define TCP_MSS 1460
#define TCP_SND_BUF (8 * TCP_MSS)
#define TCP_SND_QUEUELEN ((4 * (TCP_SND_BUF) + (TCP_MSS - 1)) / (TCP_MSS))

#define LWIP_CHKSUM_ALGORITHM 3

/* Statistics cost RAM and are only reachable through a debugger here. */
#define LWIP_STATS 0
#define LWIP_STATS_DISPLAY 0

#endif /* LWIPOPTS_H */
