/*
 * modbus_pdu.h - the part of Modbus that does not depend on the transport.
 *
 * An RTU frame is [unit][PDU][CRC16]; a TCP frame is [MBAP 7 bytes][PDU],
 * whose seventh MBAP byte is the unit identifier. So a TCP frame from offset 6
 * has exactly the RTU layout minus the checksum, and one implementation serves
 * both: the transport is left holding only framing, addressing and delivery.
 *
 * Splitting it out this way is also what makes the request handling testable
 * on the host - it has no SDK dependency, so tests/test_modbus_pdu.c can feed
 * it frames directly rather than driving a UART.
 */
#ifndef MODBUS_PDU_H
#define MODBUS_PDU_H

#include <stdint.h>

/* Longest request or response, which is a 2000-bit read reply at 253 bytes.
 * Both buffers passed below must be at least this large. */
#define MODBUS_PDU_MAX 256

/*
 * Executes one request and builds its reply. `req` starts at the unit
 * identifier, so `req[1]` is the function code.
 *
 * Returns the reply length, or 0 when there is nothing to send - a request too
 * short to carry a function code. Broadcast handling belongs to the transport:
 * RTU acts on unit 0 without replying, and TCP has no broadcast.
 */
uint16_t modbus_pdu_exec(const uint8_t *req, uint16_t req_len, uint8_t *resp);

/* MBAP header: transaction id, protocol id, length, unit id. */
#define MODBUS_TCP_MBAP 7
#define MODBUS_TCP_ADU_MAX (6 + MODBUS_PDU_MAX)

/*
 * Executes one complete Modbus TCP frame. `adu` starts at the MBAP header and
 * `len` is the whole frame - the caller owns reassembly, since TCP delivers a
 * stream rather than messages.
 *
 * Returns the reply length, or 0 for a frame that is not ours to answer: a
 * non-zero protocol identifier, or a length field disagreeing with what
 * actually arrived. Neither deserves an exception reply, which would only
 * confuse a client that is talking some other protocol at us.
 */
uint16_t modbus_tcp_adu_exec(const uint8_t *adu, uint16_t len, uint8_t *resp);

#endif /* MODBUS_PDU_H */
