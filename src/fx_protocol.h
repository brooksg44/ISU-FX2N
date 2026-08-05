/*
 * fx_protocol.h - the Mitsubishi FX programming-port protocol, over USB CDC.
 *
 * This is what GX Works 2 and GX Developer speak. It is an ASCII protocol:
 *
 *   read    PC -> PLC   STX '0' AAAA LL ETX SS
 *           PLC -> PC   STX <2*LL hex chars> ETX SS
 *   write   PC -> PLC   STX '1' AAAA LL <2*LL hex chars> ETX SS
 *           PLC -> PC   ACK
 *   force   PC -> PLC   STX '7'|'8' AAAA ETX SS      ('7' on, '8' off)
 *           PLC -> PC   ACK
 *   link    PC -> PLC   ENQ
 *           PLC -> PC   ACK
 *
 * AAAA and LL are ASCII hex, uppercase. SS is the low byte of the sum of
 * every byte from the command character through ETX inclusive, as two ASCII
 * hex digits. A frame that fails the sum check is answered with NAK.
 *
 * Addresses are byte addresses into the PLC's device image, not device
 * numbers - fx_addr_read_byte() holds the mapping table.
 *
 * The serial parameters GX Works uses (9600 7E1) do not matter here: the link
 * is USB CDC, which passes bytes through transparently, and every byte in this
 * protocol is 7-bit ASCII anyway.
 */
#ifndef FX_PROTOCOL_H
#define FX_PROTOCOL_H

#include <stdbool.h>
#include <stdint.h>

void fx_protocol_init(void);

/* Pumps the receive buffer and answers any complete frame. Call once per scan;
 * it never blocks. */
void fx_protocol_task(void);

/*
 * Diagnostic counters. With no terminal available - USB CDC carries the
 * protocol itself - these drive a status LED so the two failure modes can be
 * told apart without any extra hardware: bytes not arriving at all (host,
 * driver or cable) versus bytes arriving but frames being rejected (our
 * parser or the address map).
 */
/* Prints the captured frame trace once the link has been idle for 2 s, and at
 * most every 3 s. Call from the main loop; it rate-limits itself. */
void fx_protocol_idle_dump(void);

uint32_t fx_protocol_rx_bytes(void);
uint32_t fx_protocol_frames_ok(void);
uint32_t fx_protocol_frames_bad(void);

/*
 * Frame tracing. The FX2N extended address map is not fully documented in any
 * source available to this project, so the tracer records what GX Works 2
 * actually sends and lets the map be corrected against real traffic rather
 * than guesswork. Disabled unless FX_TRACE is defined at build time.
 */
#ifdef FX_TRACE
void fx_protocol_dump_trace(void);
#endif

#endif /* FX_PROTOCOL_H */
