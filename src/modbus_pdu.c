#include "modbus_pdu.h"

#include <stdbool.h>
#include <string.h>

#include "modbus_map.h"

/* Exception codes. */
#define MB_EX_ILLEGAL_FUNCTION 0x01
#define MB_EX_ILLEGAL_ADDRESS 0x02
#define MB_EX_ILLEGAL_VALUE 0x03

/* Every function's fixed part: unit, code, address, and one 16-bit field. */
#define MB_REQ_FIXED 6

static uint16_t be16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }

static uint16_t exception(const uint8_t *req, uint8_t *resp, uint8_t code) {
    resp[0] = req[0];
    resp[1] = (uint8_t)(req[1] | 0x80);
    resp[2] = code;
    return 3;
}

/* Echoes the request's first six bytes, which is the reply for every write. */
static uint16_t echo(const uint8_t *req, uint8_t *resp) {
    memcpy(resp, req, MB_REQ_FIXED);
    return MB_REQ_FIXED;
}

/* Reads a run of bits into a packed response. */
static uint16_t read_bits(const uint8_t *req, uint8_t *resp, bool discrete) {
    uint16_t start = be16(&req[2]);
    uint16_t count = be16(&req[4]);
    if (count == 0 || count > 2000) {
        return exception(req, resp, MB_EX_ILLEGAL_VALUE);
    }

    uint8_t bytes = (uint8_t)((count + 7) / 8);
    resp[0] = req[0];
    resp[1] = req[1];
    resp[2] = bytes;
    memset(&resp[3], 0, bytes);

    for (uint16_t i = 0; i < count; i++) {
        bool v = false;
        bool ok = discrete ? modbus_read_discrete((uint16_t)(start + i), &v)
                           : modbus_read_coil((uint16_t)(start + i), &v);
        if (!ok) {
            return exception(req, resp, MB_EX_ILLEGAL_ADDRESS);
        }
        if (v) {
            resp[3 + i / 8] |= (uint8_t)(1u << (i % 8));
        }
    }
    return (uint16_t)(3 + bytes);
}

static uint16_t read_words(const uint8_t *req, uint8_t *resp, bool input) {
    uint16_t start = be16(&req[2]);
    uint16_t count = be16(&req[4]);
    if (count == 0 || count > 125) {
        return exception(req, resp, MB_EX_ILLEGAL_VALUE);
    }

    resp[0] = req[0];
    resp[1] = req[1];
    resp[2] = (uint8_t)(count * 2);

    for (uint16_t i = 0; i < count; i++) {
        uint16_t v = 0;
        bool ok = input ? modbus_read_input((uint16_t)(start + i), &v)
                        : modbus_read_holding((uint16_t)(start + i), &v);
        if (!ok) {
            return exception(req, resp, MB_EX_ILLEGAL_ADDRESS);
        }
        resp[3 + i * 2] = (uint8_t)(v >> 8);
        resp[4 + i * 2] = (uint8_t)(v & 0xFF);
    }
    return (uint16_t)(3 + count * 2);
}

/*
 * The multi-write requests carry a byte count and then that many bytes, so
 * they are the only ones whose length is not fixed. Checking it is what keeps
 * the payload reads inside the caller's buffer: a byte count of 254 would
 * otherwise index past a 256-byte RTU frame.
 */
static bool payload_fits(const uint8_t *req, uint16_t req_len, uint16_t *bytes) {
    if (req_len < 7) {
        return false;
    }
    *bytes = req[6];
    return (uint32_t)7 + *bytes <= req_len;
}

uint16_t modbus_pdu_exec(const uint8_t *req, uint16_t req_len, uint8_t *resp) {
    if (req_len < 2) {
        return 0; /* not even a function code to answer about */
    }

    uint8_t function = req[1];

    /* Every function this device implements has at least the fixed part. */
    if (req_len < MB_REQ_FIXED) {
        return exception(req, resp, MB_EX_ILLEGAL_VALUE);
    }

    switch (function) {
        case 0x01: return read_bits(req, resp, false);
        case 0x02: return read_bits(req, resp, true);
        case 0x03: return read_words(req, resp, false);
        case 0x04: return read_words(req, resp, true);

        case 0x05: { /* write single coil */
            uint16_t addr = be16(&req[2]);
            uint16_t value = be16(&req[4]);
            if (value != 0x0000 && value != 0xFF00) {
                return exception(req, resp, MB_EX_ILLEGAL_VALUE);
            }
            if (!modbus_write_coil(addr, value == 0xFF00)) {
                return exception(req, resp, MB_EX_ILLEGAL_ADDRESS);
            }
            return echo(req, resp);
        }

        case 0x06: { /* write single register */
            uint16_t addr = be16(&req[2]);
            uint16_t value = be16(&req[4]);
            if (!modbus_write_holding(addr, value)) {
                return exception(req, resp, MB_EX_ILLEGAL_ADDRESS);
            }
            return echo(req, resp);
        }

        case 0x0F: { /* write multiple coils */
            uint16_t start = be16(&req[2]);
            uint16_t count = be16(&req[4]);
            uint16_t bytes;
            if (!payload_fits(req, req_len, &bytes) || count == 0 ||
                bytes != (count + 7) / 8) {
                return exception(req, resp, MB_EX_ILLEGAL_VALUE);
            }
            for (uint16_t i = 0; i < count; i++) {
                bool v = (req[7 + i / 8] >> (i % 8)) & 1u;
                if (!modbus_write_coil((uint16_t)(start + i), v)) {
                    return exception(req, resp, MB_EX_ILLEGAL_ADDRESS);
                }
            }
            return echo(req, resp);
        }

        case 0x10: { /* write multiple registers */
            uint16_t start = be16(&req[2]);
            uint16_t count = be16(&req[4]);
            uint16_t bytes;
            if (!payload_fits(req, req_len, &bytes) || count == 0 ||
                bytes != count * 2) {
                return exception(req, resp, MB_EX_ILLEGAL_VALUE);
            }
            for (uint16_t i = 0; i < count; i++) {
                if (!modbus_write_holding((uint16_t)(start + i),
                                          be16(&req[7 + i * 2]))) {
                    return exception(req, resp, MB_EX_ILLEGAL_ADDRESS);
                }
            }
            return echo(req, resp);
        }

        default:
            return exception(req, resp, MB_EX_ILLEGAL_FUNCTION);
    }
}
