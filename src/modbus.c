#include "modbus.h"

#include <string.h>

#include "hardware/uart.h"
#include "modbus_map.h"
#include "pico/stdlib.h"

#define MB_UART uart0
#define MB_TX_PIN 0
#define MB_RX_PIN 1

#define MB_BUF 256

/* Exception codes. */
#define MB_EX_ILLEGAL_FUNCTION 0x01
#define MB_EX_ILLEGAL_ADDRESS 0x02
#define MB_EX_ILLEGAL_VALUE 0x03

static uint8_t slave_addr = MODBUS_DEFAULT_SLAVE;
static uint8_t rx[MB_BUF];
static uint16_t rx_len;
static uint64_t last_byte_us;
static uint32_t frame_gap_us; /* 3.5 character times */

static uint32_t stat_frames;
static uint32_t stat_bad;

uint32_t modbus_rx_frames(void) { return stat_frames; }
uint32_t modbus_bad_frames(void) { return stat_bad; }

/* Modbus CRC-16, polynomial 0xA001, initial value 0xFFFF. This is the same
 * CRC the STM32 original used on its expansion port. */
static uint16_t crc16(const uint8_t *p, uint16_t n) {
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < n; i++) {
        crc ^= p[i];
        for (uint8_t b = 0; b < 8; b++) {
            crc = (crc & 1) ? (uint16_t)((crc >> 1) ^ 0xA001) : (uint16_t)(crc >> 1);
        }
    }
    return crc;
}

static void send(const uint8_t *p, uint16_t n) {
    uint16_t crc = crc16(p, n);
    uart_write_blocking(MB_UART, p, n);
    uint8_t tail[2] = {(uint8_t)(crc & 0xFF), (uint8_t)(crc >> 8)};
    uart_write_blocking(MB_UART, tail, 2);
}

static void send_exception(uint8_t function, uint8_t code) {
    uint8_t r[3] = {slave_addr, (uint8_t)(function | 0x80), code};
    send(r, 3);
}

static uint16_t be16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }

/* Reads a run of bits into a packed response. */
static void handle_read_bits(uint8_t function, bool discrete) {
    uint16_t start = be16(&rx[2]);
    uint16_t count = be16(&rx[4]);
    if (count == 0 || count > 2000) {
        send_exception(function, MB_EX_ILLEGAL_VALUE);
        return;
    }

    uint8_t bytes = (uint8_t)((count + 7) / 8);
    uint8_t r[MB_BUF];
    r[0] = slave_addr;
    r[1] = function;
    r[2] = bytes;
    memset(&r[3], 0, bytes);

    for (uint16_t i = 0; i < count; i++) {
        bool v = false;
        bool ok = discrete ? modbus_read_discrete((uint16_t)(start + i), &v)
                           : modbus_read_coil((uint16_t)(start + i), &v);
        if (!ok) {
            send_exception(function, MB_EX_ILLEGAL_ADDRESS);
            return;
        }
        if (v) {
            r[3 + i / 8] |= (uint8_t)(1u << (i % 8));
        }
    }
    send(r, (uint16_t)(3 + bytes));
}

static void handle_read_words(uint8_t function, bool input) {
    uint16_t start = be16(&rx[2]);
    uint16_t count = be16(&rx[4]);
    if (count == 0 || count > 125) {
        send_exception(function, MB_EX_ILLEGAL_VALUE);
        return;
    }

    uint8_t r[MB_BUF];
    r[0] = slave_addr;
    r[1] = function;
    r[2] = (uint8_t)(count * 2);

    for (uint16_t i = 0; i < count; i++) {
        uint16_t v = 0;
        bool ok = input ? modbus_read_input((uint16_t)(start + i), &v)
                        : modbus_read_holding((uint16_t)(start + i), &v);
        if (!ok) {
            send_exception(function, MB_EX_ILLEGAL_ADDRESS);
            return;
        }
        r[3 + i * 2] = (uint8_t)(v >> 8);
        r[4 + i * 2] = (uint8_t)(v & 0xFF);
    }
    send(r, (uint16_t)(3 + count * 2));
}

static void handle_frame(void) {
    uint8_t function = rx[1];

    switch (function) {
        case 0x01: handle_read_bits(function, false); return;
        case 0x02: handle_read_bits(function, true); return;
        case 0x03: handle_read_words(function, false); return;
        case 0x04: handle_read_words(function, true); return;

        case 0x05: { /* write single coil */
            uint16_t addr = be16(&rx[2]);
            uint16_t value = be16(&rx[4]);
            if (value != 0x0000 && value != 0xFF00) {
                send_exception(function, MB_EX_ILLEGAL_VALUE);
                return;
            }
            if (!modbus_write_coil(addr, value == 0xFF00)) {
                send_exception(function, MB_EX_ILLEGAL_ADDRESS);
                return;
            }
            send(rx, 6); /* echo the request */
            return;
        }

        case 0x06: { /* write single register */
            uint16_t addr = be16(&rx[2]);
            uint16_t value = be16(&rx[4]);
            if (!modbus_write_holding(addr, value)) {
                send_exception(function, MB_EX_ILLEGAL_ADDRESS);
                return;
            }
            send(rx, 6);
            return;
        }

        case 0x0F: { /* write multiple coils */
            uint16_t start = be16(&rx[2]);
            uint16_t count = be16(&rx[4]);
            uint8_t bytes = rx[6];
            if (count == 0 || bytes != (count + 7) / 8) {
                send_exception(function, MB_EX_ILLEGAL_VALUE);
                return;
            }
            for (uint16_t i = 0; i < count; i++) {
                bool v = (rx[7 + i / 8] >> (i % 8)) & 1u;
                if (!modbus_write_coil((uint16_t)(start + i), v)) {
                    send_exception(function, MB_EX_ILLEGAL_ADDRESS);
                    return;
                }
            }
            uint8_t r[6] = {slave_addr, function, rx[2], rx[3], rx[4], rx[5]};
            send(r, 6);
            return;
        }

        case 0x10: { /* write multiple registers */
            uint16_t start = be16(&rx[2]);
            uint16_t count = be16(&rx[4]);
            uint8_t bytes = rx[6];
            if (count == 0 || bytes != count * 2) {
                send_exception(function, MB_EX_ILLEGAL_VALUE);
                return;
            }
            for (uint16_t i = 0; i < count; i++) {
                if (!modbus_write_holding((uint16_t)(start + i), be16(&rx[7 + i * 2]))) {
                    send_exception(function, MB_EX_ILLEGAL_ADDRESS);
                    return;
                }
            }
            uint8_t r[6] = {slave_addr, function, rx[2], rx[3], rx[4], rx[5]};
            send(r, 6);
            return;
        }

        default:
            send_exception(function, MB_EX_ILLEGAL_FUNCTION);
            return;
    }
}

void modbus_init(uint8_t address, uint32_t baud) {
    slave_addr = address;

    uart_init(MB_UART, baud);
    gpio_set_function(MB_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(MB_RX_PIN, GPIO_FUNC_UART);
    uart_set_format(MB_UART, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(MB_UART, true);

    /* 3.5 character times at 11 bits per character. */
    frame_gap_us = (uint32_t)((11u * 3500000u) / baud);

    rx_len = 0;
    last_byte_us = time_us_64();
}

void modbus_task(void) {
    while (uart_is_readable(MB_UART)) {
        uint8_t b = uart_getc(MB_UART);
        if (rx_len < MB_BUF) {
            rx[rx_len++] = b;
        }
        last_byte_us = time_us_64();
    }

    if (rx_len == 0 || (time_us_64() - last_byte_us) < frame_gap_us) {
        return; /* still receiving */
    }

    /* Silence has ended the frame. */
    uint16_t len = rx_len;
    rx_len = 0;

    if (len < 4) {
        stat_bad++;
        return;
    }
    if (crc16(rx, (uint16_t)(len - 2)) !=
        (uint16_t)(rx[len - 2] | (rx[len - 1] << 8))) {
        stat_bad++;
        return;
    }
    /* Address 0 is a broadcast: act on it, but never reply. */
    if (rx[0] != slave_addr && rx[0] != 0) {
        return;
    }

    stat_frames++;
    if (rx[0] == 0) {
        return; /* broadcast writes are not acknowledged */
    }
    handle_frame();
}
