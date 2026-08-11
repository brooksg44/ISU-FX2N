/*
 * Host-side tests for transport-independent Modbus request handling.
 *
 * Requests here start at the unit identifier, which is the RTU frame without
 * its CRC and the TCP frame from the seventh MBAP byte onwards - so what these
 * exercise is equally what a TCP transport will hand over.
 */
#include <stdio.h>
#include <string.h>

#include "modbus_map.h"
#include "modbus_pdu.h"
#include "plc_memory.h"

#define UNIT 1

static int failures;
static int checks;

static void check(const char *what, long expected, long actual) {
    checks++;
    if (expected != actual) {
        failures++;
        printf("  FAIL %-52s expected %ld, got %ld\n", what, expected, actual);
    }
}

/* Runs one request, returning the reply length and filling `resp`. */
static uint16_t exec(const uint8_t *req, uint16_t len, uint8_t *resp) {
    memset(resp, 0, MODBUS_PDU_MAX);
    return modbus_pdu_exec(req, len, resp);
}

static void test_reads_coils_from_y(void) {
    uint8_t resp[MODBUS_PDU_MAX];
    /* read 4 coils from 0 -> Y0..Y3 */
    const uint8_t req[] = {UNIT, 0x01, 0x00, 0x00, 0x00, 0x04};

    plc_memory_init();
    plc_set_y(0, true);
    plc_set_y(2, true);

    check("coil read replies with unit, code, count and data", 4,
          exec(req, sizeof req, resp));
    check("coil read echoes the unit", UNIT, resp[0]);
    check("coil read echoes the function", 0x01, resp[1]);
    check("coil read reports one data byte", 1, resp[2]);
    check("coil read packs Y0 and Y2", 0x05, resp[3]);
}

static void test_writes_a_holding_register(void) {
    uint8_t resp[MODBUS_PDU_MAX];
    /* write 0x1234 to holding 100 -> D100 */
    const uint8_t req[] = {UNIT, 0x06, 0x00, 0x64, 0x12, 0x34};

    plc_memory_init();

    check("single register write echoes the request", 6,
          exec(req, sizeof req, resp));
    check("single register write reaches D100", 0x1234, plc_get_d(100));
    check("single register write echoes byte for byte", 0,
          memcmp(resp, req, sizeof req));
}

static void test_reports_an_address_outside_the_map(void) {
    uint8_t resp[MODBUS_PDU_MAX];
    /* coil 900 is between the Y block and the M block */
    const uint8_t req[] = {UNIT, 0x05, 0x03, 0x84, 0xFF, 0x00};

    plc_memory_init();

    check("unmapped address replies with an exception", 3,
          exec(req, sizeof req, resp));
    check("exception sets the high bit on the function", 0x85, resp[1]);
    check("exception is illegal data address", 0x02, resp[2]);
}

static void test_reports_an_unsupported_function(void) {
    uint8_t resp[MODBUS_PDU_MAX];
    const uint8_t req[] = {UNIT, 0x2B, 0x00, 0x00, 0x00, 0x01};

    plc_memory_init();

    check("unknown function replies with an exception", 3,
          exec(req, sizeof req, resp));
    check("unknown function exception is illegal function", 0x01, resp[2]);
}

static void test_writes_multiple_registers(void) {
    uint8_t resp[MODBUS_PDU_MAX];
    const uint8_t req[] = {UNIT, 0x10, 0x00, 0x0A, 0x00, 0x02,
                           0x04, 0xAA, 0xBB, 0xCC, 0xDD};

    plc_memory_init();

    check("multi register write echoes six bytes", 6,
          exec(req, sizeof req, resp));
    check("multi register write reaches D10", 0xAABB, plc_get_d(10));
    check("multi register write reaches D11", 0xCCDD, plc_get_d(11));
}

/*
 * The byte count is attacker-controlled and the payload reads are indexed by
 * it. A frame claiming more payload than it carries used to be believed: with
 * a byte count of 254 the register loop indexed req[7..259], past the end of
 * the 256-byte receive buffer. It must be refused instead.
 */
static void test_refuses_a_payload_longer_than_the_frame(void) {
    uint8_t resp[MODBUS_PDU_MAX];
    /* claims 127 registers and 254 payload bytes, carries none of them */
    const uint8_t req[] = {UNIT, 0x10, 0x00, 0x0A, 0x00, 0x7F, 0xFE};

    plc_memory_init();
    plc_set_d(10, 0x1111);

    check("overlong payload replies with an exception", 3,
          exec(req, sizeof req, resp));
    check("overlong payload is illegal data value", 0x03, resp[2]);
    check("overlong payload writes nothing", 0x1111, plc_get_d(10));
}

static void test_refuses_a_truncated_coil_payload(void) {
    uint8_t resp[MODBUS_PDU_MAX];
    /* claims 16 coils and 2 payload bytes, carries one */
    const uint8_t req[] = {UNIT, 0x0F, 0x00, 0x00, 0x00, 0x10, 0x02, 0xFF};

    plc_memory_init();

    check("truncated coil payload replies with an exception", 3,
          exec(req, sizeof req, resp));
    check("truncated coil payload is illegal data value", 0x03, resp[2]);
    check("truncated coil payload writes nothing", 0, plc_get_y(0));
}

/* A request too short to name an address cannot be answered meaningfully, and
 * must not be answered from whatever the buffer happened to hold. */
static void test_refuses_a_short_request(void) {
    uint8_t resp[MODBUS_PDU_MAX];
    const uint8_t req[] = {UNIT, 0x03, 0x00};

    plc_memory_init();

    check("short request replies with an exception", 3,
          exec(req, sizeof req, resp));
    check("short request is illegal data value", 0x03, resp[2]);

    const uint8_t stub[] = {UNIT};
    check("request without a function code gets no reply", 0,
          exec(stub, sizeof stub, resp));
}

/* Quantity limits come from the protocol, not from our buffer sizes. */
static void test_enforces_the_quantity_limits(void) {
    uint8_t resp[MODBUS_PDU_MAX];
    const uint8_t too_many_bits[] = {UNIT, 0x01, 0x00, 0x00, 0x07, 0xD1};
    const uint8_t too_many_words[] = {UNIT, 0x03, 0x00, 0x00, 0x00, 0x7E};
    const uint8_t none_at_all[] = {UNIT, 0x03, 0x00, 0x00, 0x00, 0x00};

    plc_memory_init();

    check("2001 bits is refused", 0x03, (exec(too_many_bits, 6, resp), resp[2]));
    check("126 words is refused", 0x03, (exec(too_many_words, 6, resp), resp[2]));
    check("zero quantity is refused", 0x03, (exec(none_at_all, 6, resp), resp[2]));
}

int main(void) {
    test_reads_coils_from_y();
    test_writes_a_holding_register();
    test_reports_an_address_outside_the_map();
    test_reports_an_unsupported_function();
    test_writes_multiple_registers();
    test_refuses_a_payload_longer_than_the_frame();
    test_refuses_a_truncated_coil_payload();
    test_refuses_a_short_request();
    test_enforces_the_quantity_limits();
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
