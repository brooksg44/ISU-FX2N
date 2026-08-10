#include "fx_protocol.h"

#include <stdio.h>

#include "fx_addr.h"
#include "fx_monitor.h"
#include "plc_memory.h"
#include "plc_exec.h"
#include "plc_program.h"
#include "plc_storage.h"
#include "plc_scan.h"
#include "pico/stdlib.h"

#define STX 0x02
#define ETX 0x03
#define ENQ 0x05
#define ACK 0x06
#define NAK 0x15

/* A write carrying the 64-byte maximum payload is the longest frame. */
#define FX_BUF_SIZE 256

static uint8_t rx[FX_BUF_SIZE];
static uint16_t rx_len;
static bool in_frame;

static uint32_t stat_rx_bytes;
static uint32_t stat_frames_ok;
static uint32_t stat_frames_bad;

/* E41 announces the compiled-program range. In the observed download GX
 * follows it with a two-byte E11 transaction at range_start + 2 containing a
 * transfer check value, not a replacement instruction word. */
static bool program_check_pending;
static uint16_t program_check_addr;

/*
 * Frame tracer. The FX2N extended address map is not fully documented in any
 * source available to this project, so rather than guess at what GX Works 2
 * sends, we record it. Frames are kept in RAM and dumped on demand, which
 * avoids needing a second serial port: disconnect GX Works, open any terminal
 * on the same COM port, and press '?'.
 *
 * 24 bytes is enough to carry STX, the command, the address and the length,
 * which is what identifies an unrecognised request.
 */
#define TRACE_FRAMES 12
#define TRACE_BYTES 144

static struct {
    uint8_t len;
    uint8_t truncated;
    bool ok;
    uint8_t data[TRACE_BYTES];
} trace[TRACE_FRAMES];

static uint8_t trace_head;
static uint8_t trace_count;

/*
 * While GX Works is merely monitoring it sends the same '0' reads forever, so
 * recording everything would show nothing but polling. Instead the routine
 * '0' reads are dropped and everything else - extended commands, writes,
 * forces, anything unrecognised - is kept in a rolling window.
 *
 * Keeping the TAIL matters: a download runs longer than the buffer, and the
 * frame we mishandle is at the point it gives up, not at the start.
 */
static void trace_record(const uint8_t *f, uint16_t len, bool ok) {
    /*
     * Drop routine reads - both the plain '0' identity polls and the 'E0'
     * extended reads that Monitor Mode sends continuously. Without this the
     * ring fills with monitoring traffic and a one-off frame (a force, a
     * remote run/stop) scrolls away before it can be read back.
     *
     * Rejected frames are always kept, whatever they are.
     */
    /*
     * Drop only the three exchanges known to repeat forever, and only when
     * they succeeded. Anything else - including an extended read of an
     * unfamiliar address - is kept, because an over-eager filter hides the
     * very frame being hunted.
     *
     *   '0' ....             identity polling of D8001/D8101
     *   'E' "00" "0Exx"      extended read of the D8000 block
     *   'E' "10" "14xx"      Monitor Mode writing its watch list
     */
    if (ok && len > 7 && f[0] == STX) {
        if (f[1] == '0') {
            return;
        }
        if (f[1] == 'E' && f[2] == '0' && f[3] == '0' && f[4] == '0' && f[5] == 'E') {
            return;
        }
        if (f[1] == 'E' && f[2] == '1' && f[3] == '0' && f[4] == '1' && f[5] == '4') {
            return;
        }
    }
    uint8_t n = len > TRACE_BYTES ? TRACE_BYTES : (uint8_t)len;
    trace[trace_head].len = n;
    trace[trace_head].truncated = len > TRACE_BYTES;
    trace[trace_head].ok = ok;
    for (uint8_t i = 0; i < n; i++) {
        trace[trace_head].data[i] = f[i];
    }
    trace_head = (uint8_t)((trace_head + 1) % TRACE_FRAMES);
    if (trace_count < TRACE_FRAMES) {
        trace_count++;
    }
}

uint32_t fx_protocol_rx_bytes(void) { return stat_rx_bytes; }
uint32_t fx_protocol_frames_ok(void) { return stat_frames_ok; }
uint32_t fx_protocol_frames_bad(void) { return stat_frames_bad; }

static uint8_t hex_digit(uint8_t nibble) {
    return (uint8_t)(nibble < 10 ? '0' + nibble : 'A' + (nibble - 10));
}

static bool hex_value(uint8_t ch, uint8_t *out) {
    if (ch >= '0' && ch <= '9') {
        *out = (uint8_t)(ch - '0');
    } else if (ch >= 'A' && ch <= 'F') {
        *out = (uint8_t)(ch - 'A' + 10);
    } else if (ch >= 'a' && ch <= 'f') {
        *out = (uint8_t)(ch - 'a' + 10);
    } else {
        return false;
    }
    return true;
}

static bool parse_hex(const uint8_t *p, uint8_t n, uint32_t *out) {
    uint32_t v = 0;
    for (uint8_t i = 0; i < n; i++) {
        uint8_t d;
        if (!hex_value(p[i], &d)) {
            return false;
        }
        v = (v << 4) | d;
    }
    *out = v;
    return true;
}

/* Sum check covers CMD..ETX inclusive; only the low byte is transmitted. */
static uint8_t checksum(const uint8_t *p, uint16_t n) {
    uint8_t sum = 0;
    for (uint16_t i = 0; i < n; i++) {
        sum = (uint8_t)(sum + p[i]);
    }
    return sum;
}

static void send_byte(uint8_t b) { putchar_raw(b); }

static void send_simple(uint8_t b) {
    send_byte(b);
    stdio_flush();
}

static void send_frame(const uint8_t *frame, uint16_t len) {
    /* Give the complete response to the Pico stdio driver in one operation.
     * Its USB backend advances TinyUSB while waiting for endpoint space.
     * Calling tud_cdc_write() directly bypassed that service loop and stalled
     * after the first nine bytes on the Windows CDC connection. */
    stdio_put_string((const char *)frame, len, false, false);
    stdio_flush();
}

/* A rejected frame is worth counting separately: it means bytes are arriving
 * but we are failing to understand them. */
static void reject(void) {
    stat_frames_bad++;
    send_simple(NAK);
}

static void send_read_response(uint16_t addr, uint8_t count) {
    /* Largest observed reply is an E4 read of 64 words = 128 bytes. Build the
     * complete ASCII frame before handing it to USB. Sending one character at
     * a time made 64-byte reads intermittently stop at a USB packet boundary,
     * leaving GX Works without ETX/checksum even though the request was
     * counted as successful. */
    uint8_t frame[1 + 128 * 2 + 1 + 2];
    uint16_t n = 0;
    uint8_t sum = 0;
    frame[n++] = STX;
    for (uint8_t i = 0; i < count; i++) {
        uint16_t a = (uint16_t)(addr + i);
        uint8_t v = fx_monitor_owns_read(a) ? fx_monitor_read(a)
                                            : fx_addr_read_byte(a);
        uint8_t hi = hex_digit((uint8_t)(v >> 4));
        uint8_t lo = hex_digit((uint8_t)(v & 0x0F));
        frame[n++] = hi;
        frame[n++] = lo;
        sum = (uint8_t)(sum + hi + lo);
    }
    frame[n++] = ETX;
    sum = (uint8_t)(sum + ETX);
    frame[n++] = hex_digit((uint8_t)(sum >> 4));
    frame[n++] = hex_digit((uint8_t)(sum & 0x0F));
    send_frame(frame, n);
}

/* E4 starts a program upload range. A known-good FX emulator replies with the
 * requested memory-space digit and starting address, followed by the normal
 * ETX/checksum. For example E41 805C 0F00 -> STX "1805C" ETX "14". */
static void send_upload_range_response(uint8_t space, uint16_t addr) {
    uint8_t frame[9];
    uint8_t n = 0;
    uint8_t sum = 0;
    frame[n++] = STX;
    frame[n++] = space;
    sum = (uint8_t)(sum + space);
    for (int shift = 12; shift >= 0; shift -= 4) {
        uint8_t ch = hex_digit((uint8_t)((addr >> shift) & 0x0F));
        frame[n++] = ch;
        sum = (uint8_t)(sum + ch);
    }
    frame[n++] = ETX;
    sum = (uint8_t)(sum + ETX);
    frame[n++] = hex_digit((uint8_t)(sum >> 4));
    frame[n++] = hex_digit((uint8_t)(sum & 0x0F));
    send_frame(frame, n);
}

/* rx holds a complete frame: STX ... ETX SS SS. */
static void handle_frame(void) {
    if (rx_len < 5 || rx[0] != STX) {
        reject();
        return;
    }

    uint16_t etx_pos = (uint16_t)(rx_len - 3);
    if (rx[etx_pos] != ETX) {
        reject();
        return;
    }

    uint32_t got;
    if (!parse_hex(&rx[etx_pos + 1], 2, &got) ||
        checksum(&rx[1], etx_pos) != (uint8_t)got) {
        reject();
        return;
    }

    uint8_t cmd = rx[1];
    uint32_t addr, count;

    switch (cmd) {
        case '0': /* device read: '0' AAAA LL */
            if (etx_pos != 8 || !parse_hex(&rx[2], 4, &addr) ||
                !parse_hex(&rx[6], 2, &count) || count == 0 || count > 64) {
                reject();
                return;
            }
            stat_frames_ok++;
            send_read_response((uint16_t)addr, (uint8_t)count);
            return;

        case '1': /* device write: '1' AAAA LL <data> */
            if (etx_pos < 8 || !parse_hex(&rx[2], 4, &addr) ||
                !parse_hex(&rx[6], 2, &count) || count == 0 || count > 64 ||
                etx_pos != 8 + count * 2) {
                reject();
                return;
            }
            for (uint32_t i = 0; i < count; i++) {
                uint32_t v;
                if (!parse_hex(&rx[8 + i * 2], 2, &v)) {
                    reject();
                    return;
                }
                fx_addr_write_byte((uint16_t)(addr + i), (uint8_t)v);
            }
            stat_frames_ok++;
            send_simple(ACK);
            return;

        /*
         * Extended read. Observed from GX Works 2 during a download:
         *
         *   02 'E' "01" "8000" "40" 03 "D5"   64 bytes from 0x8000
         *   02 'E' "01" "8040" "1C" 03 "E9"   28 bytes from 0x8040
         *   02 'E' "00" "0E00" "40" 03 "E1"   64 bytes from 0x0E00
         *        subcmd  addr   count
         *
         *   02 'E' "11" "8000" "40" <64 bytes> 03 "94"   parameter download
         *
         * Our address map already separates the two spaces by address, since
         * program memory lives at 0x8000 and above, so both route through the
         * same lookup regardless of which space the subcommand names.
         *
         * Subcommands outside 00/01/10/11 are rejected on purpose, so the
         * trace reveals them rather than us silently answering something we do
         * not understand.
         */
        case 'E': {
            uint32_t sub;

            /*
             * Program-upload range command used by GX Works 2:
             *
             *   02 'E' '4' '1' "805C" "0F00" 03 "63"
             *            space  address  15 words, little-endian
             *
             * The reply echoes "1805C" (space and address); GX subsequently
             * uses E01 to transfer the actual data. Keeping this separate
             * from E00/E01 also prevents the extra count byte being mistaken
             * for payload.
             */
            if (etx_pos == 12 && rx[2] == '4' &&
                (rx[3] == '0' || rx[3] == '1')) {
                uint32_t count_lo, count_hi;
                if (!parse_hex(&rx[4], 4, &addr) ||
                    !parse_hex(&rx[8], 2, &count_lo) ||
                    !parse_hex(&rx[10], 2, &count_hi)) {
                    reject();
                    return;
                }
                uint32_t words = count_lo | (count_hi << 8);
                if (words == 0 || words > 64 ||
                    (rx[3] == '1' && addr < FX_ADDR_PROGRAM)) {
                    reject();
                    return;
                }
                if (rx[3] == '1') {
                    program_check_pending = true;
                    program_check_addr = (uint16_t)(addr + 2u);
                }
                stat_frames_ok++;
                send_upload_range_response(rx[3], (uint16_t)addr);
                return;
            }

            /* Extended force, a different shape from read/write forms. The
             * address is little-endian: "240E" is special relay M8036.
             * GX Works Remote Operation uses the standard FX relay sequence:
             *
             *   RUN:  M8037 OFF, M8035 ON, M8036 ON
             *   STOP: M8037 ON
             *
             * The relay image is updated as for any other force. M8036 and
             * M8037 additionally control the emulator scan mode. */
            if (etx_pos == 7 && (rx[2] == '7' || rx[2] == '8')) {
                uint32_t raw;
                if (parse_hex(&rx[3], 4, &raw)) {
                    /* Sent little-endian: "2C01" means 0x012C. */
                    uint16_t a = (uint16_t)(((raw & 0xFF) << 8) | (raw >> 8));
                    bool on = rx[2] == '7';
                    fx_addr_force(a, on);
                    if (on && a == FX_FORCE_MS_BASE + 36u) {
                        plc_scan_set_mode(PLC_MODE_RUN);   /* M8036 */
                    } else if (on && a == FX_FORCE_MS_BASE + 37u) {
                        plc_scan_set_mode(PLC_MODE_STOP);  /* M8037 */
                    }
                }
                stat_frames_ok++;
                send_simple(ACK);
                return;
            }

            if (etx_pos < 10 || !parse_hex(&rx[2], 2, &sub) ||
                !parse_hex(&rx[4], 4, &addr) || !parse_hex(&rx[8], 2, &count) ||
                count == 0 || count > 64) {
                reject();
                return;
            }
            /*
             * The subcommand is two independent nibbles:
             *   low  0 = device memory, 1 = program memory
             *   high 0 = read,          1 = write
             * giving 00/01 read and 10/11 write. Observed directly: a
             * parameter download sends E 11 8000 40 <64 bytes>.
             */
            bool is_write = (sub & 0xF0) == 0x10;
            if ((sub & 0x0F) > 0x01 || (sub & 0xF0) > 0x10) {
                reject();
                return;
            }
            if (is_write) {
                if (etx_pos != 10 + count * 2) {
                    reject();
                    return;
                }
                /*
                 * FSM_STL capture:
                 *   E41 805C 0F00       announce 15-word program range
                 *   E11 805E 02 0FB4   transfer check/finalization value
                 *
                 * Treating the latter as ordinary memory replaced the
                 * already-downloaded 0x0006 SET-S instruction with 0xB40F,
                 * so M8002 could no longer initialise S10. Acknowledge this
                 * narrowly identified post-E41 transaction without storing
                 * it in the instruction image.
                 */
                if (program_check_pending && count == 2 &&
                    addr == program_check_addr) {
                    program_check_pending = false;
                    stat_frames_ok++;
                    send_simple(ACK);
                    return;
                }
                program_check_pending = false;
                for (uint32_t i = 0; i < count; i++) {
                    uint32_t v;
                    if (!parse_hex(&rx[10 + i * 2], 2, &v)) {
                        reject();
                        return;
                    }
                    uint16_t a = (uint16_t)(addr + i);
                    if (fx_monitor_owns_write(a)) {
                        fx_monitor_write(a, (uint8_t)v);
                    } else {
                        fx_addr_write_byte(a, (uint8_t)v);
                    }
                }
                if (addr >= FX_ADDR_PROGRAM) {
                    plc_storage_mark_dirty();
                }
                stat_frames_ok++;
                send_simple(ACK);
                return;
            }
            if (etx_pos != 10) {
                reject();
                return;
            }
            if (addr == FX_MON_RESULT) {
                fx_monitor_sample();
            }
            stat_frames_ok++;
            send_read_response((uint16_t)addr, (uint8_t)count);
            return;
        }

        /*
         * Bare command with no payload, sent immediately after the parameter
         * download and before any program write: 02 'B' 03 "45".
         *
         * Read as remote STOP - a download requires a stopped PLC, and GX
         * Works offers to put it back in RUN afterwards, which implies a
         * matching resume command we have not seen yet. Treated as STOP on
         * that basis; if the trace later shows 'B' being used to resume, this
         * is the place to correct.
         */
        case 'B':
            if (etx_pos != 2) {
                reject();
                return;
            }
            plc_scan_set_mode(PLC_MODE_STOP);
            stat_frames_ok++;
            send_simple(ACK);
            return;

        case '7': /* force on  */
        case '8': /* force off */
            if (etx_pos != 6 || !parse_hex(&rx[2], 4, &addr)) {
                reject();
                return;
            }
            /* The force commands carry the bit address with its two bytes
             * swapped relative to the read/write commands. This is the least
             * certain part of the protocol layer and wants validating against
             * a real GX Works "Debug -> Force ON" before it is trusted. */
            {
                uint16_t a = (uint16_t)(((addr & 0x00FF) << 8) | (addr >> 8));
                fx_addr_write_bit((uint16_t)(a / 8), (uint8_t)(a % 8), cmd == '7');
            }
            stat_frames_ok++;
            send_simple(ACK);
            return;

        default:
            reject();
            return;
    }
}

/* Human-readable dump, printed with explicit CRLF because CRLF translation is
 * turned off for the binary protocol. */
static void trace_dump(void) {
    printf("\r\n=== ISU-FX2N ===\r\n");
    printf("identity: D8001=%u D8101=%u (FX2N)\r\n", plc_get_d(D_PLC_TYPE),
           plc_get_d(D_MODEL_CODE));
    printf("capacity: D8002=%u D8003=%u D8102=%u\r\n",
           plc_get_d(D_MEMORY_CAPACITY), plc_get_d(8003), plc_get_d(8102));
    printf("program: %s, unknown opcodes this scan: %u (last 0x%04X)\r\n",
           plc_exec_has_program() ? "present" : "none (running built-in demo)",
           plc_exec_unknown_count(), plc_exec_last_bad_opcode());

    printf("program memory (0x8000 + offset):\r\n");
    for (uint16_t off = 0; off < 0x180; off += 16) {
        /* Skip erased rows - they are the bulk of the dump. */
        bool blank = true;
        for (uint8_t i = 0; i < 16; i++) {
            if (plc_program_read((uint32_t)(off + i)) != 0xFF) {
                blank = false;
                break;
            }
        }
        if (blank) {
            continue;
        }
        printf("  +%04X ", off);
        for (uint8_t i = 0; i < 16; i++) {
            printf("%02X ", plc_program_read((uint32_t)(off + i)));
        }
        printf("\r\n");
    }

    /*
     * Frames go LAST on purpose: this is the part that usually needs copying
     * out of a terminal, and anything printed after it just pushes it out of
     * view.
     */
    printf("--- frame trace: rx %lu bytes, ok %lu, rejected %lu ---\r\n",
           (unsigned long)stat_rx_bytes, (unsigned long)stat_frames_ok,
           (unsigned long)stat_frames_bad);

    uint8_t start = (uint8_t)((trace_head + TRACE_FRAMES - trace_count) % TRACE_FRAMES);
    for (uint8_t k = 0; k < trace_count; k++) {
        uint8_t i = (uint8_t)((start + k) % TRACE_FRAMES);
        printf("%2u %-3s ", k, trace[i].ok ? "ok" : "NAK");
        for (uint8_t j = 0; j < trace[i].len; j++) {
            printf("%02X ", trace[i].data[j]);
        }
        printf(" |");
        for (uint8_t j = 0; j < trace[i].len; j++) {
            uint8_t c = trace[i].data[j];
            printf("%c", (c >= 0x20 && c < 0x7F) ? c : '.');
        }
        printf("|%s\r\n", trace[i].truncated ? " (truncated)" : "");
    }
    if (trace_count == 0) {
        printf("(no non-routine frames captured)\r\n");
    }

    /* Some serial-monitor tools retain only the tail of a burst. Repeat a
     * compact execution summary last, together with the two areas relevant
     * to FSM_EQU's initial and end-of-scan MOVE_E instructions. */
    printf("--- compact execution summary ---\r\n");
    printf("mode=%s M8000=%u M8002=%u D100=%u D101=%u "
           "unknown=%u last=0x%04X\r\n",
           plc_scan_get_mode() == PLC_MODE_RUN ? "RUN" : "STOP",
           plc_get_m(M_RUN_MONITOR), plc_get_m(M_INITIAL_PULSE),
           plc_get_d(100), plc_get_d(101), plc_exec_unknown_count(),
           plc_exec_last_bad_opcode());
    printf("FSM_Counter: X0=%u M1486=%u M1487=%u M1503=%u "
           "D995(CV)=%u D997(index)=%u D998(copy)=%u\r\n",
           plc_get_x(0), plc_get_m(1486), plc_get_m(1487), plc_get_m(1503),
           plc_get_d(995), plc_get_d(997), plc_get_d(998));
    printf("code+005C:");
    for (uint16_t off = 0x5C; off < 0x7C; off++) {
        printf(" %02X", plc_program_read(off));
    }
    printf("\r\ncode+019C:");
    for (uint16_t off = 0x19C; off < 0x1BC; off++) {
        printf(" %02X", plc_program_read(off));
    }
    printf("\r\nmonitor-list:");
    for (uint16_t off = 0; off < 32; off++) {
        printf(" %02X", fx_monitor_list_byte(off));
    }
    printf("\r\nmonitor-result:");
    for (uint16_t off = 0; off < fx_monitor_result_len(); off++) {
        printf(" %02X", fx_monitor_result_byte(off));
    }
    printf("\r\n");
    printf("=== end ===\r\n");
    stdio_flush();
}

void fx_protocol_idle_dump(void) {
    /*
     * Intentionally empty. GX Works closes and reopens the COM port between
     * download and Monitor Mode. A capture of FSM_STL showed a 2.94 s gap;
     * the former two-second idle dump therefore put diagnostic text in the
     * CDC transmit queue. On reconnect GX Works sent ENQ but received the
     * queued ASCII text instead of ACK, so Monitor Mode could not start.
     *
     * Diagnostics remain available with the explicit bare '?' command after
     * GX Works has been disconnected. Protocol output must otherwise only be
     * emitted in response to protocol input.
     */
}

void fx_protocol_init(void) {
    rx_len = 0;
    in_frame = false;
    program_check_pending = false;
    /* Raw binary channel: stdio must not rewrite \n as \r\n. */
    stdio_set_translate_crlf(&stdio_usb, false);
}

void fx_protocol_task(void) {
    int ch;
    while ((ch = getchar_timeout_us(0)) != PICO_ERROR_TIMEOUT) {
        uint8_t b = (uint8_t)ch;
        stat_rx_bytes++;

        if (b == ENQ && !in_frame) {
            stat_frames_ok++;
            trace_record(&b, 1, true);
            send_simple(ACK);
            continue;
        }

        /* Debug escape, safe because GX Works never sends a bare '?' outside
         * a frame. Disconnect GX Works first, then press '?' in a terminal. */
        if (b == '?' && !in_frame) {
            trace_dump();
            continue;
        }

        if (b == STX) {
            in_frame = true;
            rx_len = 0;
        }
        if (!in_frame) {
            continue;
        }

        if (rx_len >= FX_BUF_SIZE) {
            in_frame = false; /* overlong - resynchronise on the next STX */
            rx_len = 0;
            continue;
        }
        rx[rx_len++] = b;

        /* A frame ends two sum-check characters after ETX. */
        if (rx_len >= 4 && rx[rx_len - 3] == ETX) {
            uint32_t bad_before = stat_frames_bad;
            handle_frame();
            trace_record(rx, rx_len, stat_frames_bad == bad_before);
            in_frame = false;
            rx_len = 0;
        }
    }
}
