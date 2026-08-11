#include "modbus.h"

#include "hardware/uart.h"
#include "modbus_pdu.h"
#include "pico/stdlib.h"

#define MB_UART uart0
#define MB_TX_PIN 0
#define MB_RX_PIN 1

#define MB_BUF MODBUS_PDU_MAX

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

    /* The CRC is the transport's, so the request handed on is the frame
     * without it - which is exactly the layout a Modbus TCP frame has from
     * its unit identifier onwards. */
    uint8_t resp[MODBUS_PDU_MAX];
    uint16_t n = modbus_pdu_exec(rx, (uint16_t)(len - 2), resp);
    if (n > 0) {
        send(resp, n);
    }
}
