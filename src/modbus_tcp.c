#include "modbus_tcp.h"

#include <string.h>

#include "lwip/tcp.h"
#include "modbus_pdu.h"

/*
 * Four concurrent clients. A trainer might have a SCADA package, a student's
 * Python script and a diagnostic tool attached at once; beyond that a refused
 * connection is a clearer failure than an allocation that might not succeed
 * mid-scan.
 */
#define MODBUS_TCP_CONNS 4

typedef struct {
    struct tcp_pcb *pcb;
    uint16_t len;
    uint8_t buf[MODBUS_TCP_ADU_MAX];
} conn_t;

static conn_t conns[MODBUS_TCP_CONNS];
static struct tcp_pcb *listener;

static uint32_t stat_connections;
static uint32_t stat_requests;

uint32_t modbus_tcp_connections(void) { return stat_connections; }
uint32_t modbus_tcp_requests(void) { return stat_requests; }

static uint16_t be16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }

static void release(conn_t *c) {
    if (c->pcb == NULL) {
        return;
    }
    tcp_arg(c->pcb, NULL);
    tcp_recv(c->pcb, NULL);
    tcp_err(c->pcb, NULL);
    tcp_close(c->pcb);
    c->pcb = NULL;
    c->len = 0;
}

/*
 * Consumes whole frames from the receive buffer.
 *
 * TCP is a stream, so a frame can arrive split across segments or several can
 * arrive in one. The MBAP length field is the only frame boundary available,
 * which is why a frame whose length is impossible ends the connection: without
 * a trustworthy boundary there is no way to resynchronise, unlike RTU where
 * silence eventually reframes the line.
 */
static bool consume(conn_t *c) {
    uint8_t resp[MODBUS_TCP_ADU_MAX];

    while (c->len >= 6) {
        uint16_t claimed = be16(&c->buf[4]);
        if (claimed == 0 || claimed > MODBUS_PDU_MAX) {
            return false;
        }

        uint16_t total = (uint16_t)(6 + claimed);
        if (c->len < total) {
            return true; /* the rest is still in flight */
        }

        uint16_t n = modbus_tcp_adu_exec(c->buf, total, resp);
        if (n > 0) {
            stat_requests++;
            if (tcp_write(c->pcb, resp, n, TCP_WRITE_FLAG_COPY) != ERR_OK) {
                return false;
            }
            tcp_output(c->pcb);
        }

        c->len = (uint16_t)(c->len - total);
        memmove(c->buf, c->buf + total, c->len);
    }
    return true;
}

static err_t on_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err) {
    conn_t *c = (conn_t *)arg;

    if (c == NULL) {
        if (p != NULL) {
            pbuf_free(p);
        }
        tcp_close(pcb);
        return ERR_OK;
    }
    if (p == NULL || err != ERR_OK) { /* remote closed, or a receive error */
        if (p != NULL) {
            pbuf_free(p);
        }
        release(c);
        return ERR_OK;
    }

    /* Refuse anything that cannot be a frame rather than growing the buffer:
     * the longest legal ADU is known, so an overrun is a client fault. */
    if (p->tot_len > (uint16_t)(sizeof c->buf - c->len)) {
        pbuf_free(p);
        release(c);
        return ERR_OK;
    }

    pbuf_copy_partial(p, c->buf + c->len, p->tot_len, 0);
    c->len = (uint16_t)(c->len + p->tot_len);
    tcp_recved(pcb, p->tot_len);
    pbuf_free(p);

    if (!consume(c)) {
        release(c);
    }
    return ERR_OK;
}

/* lwIP has already freed the pcb by the time this runs, so it must not be
 * closed again - only forgotten. */
static void on_error(void *arg, err_t err) {
    conn_t *c = (conn_t *)arg;
    (void)err;
    if (c != NULL) {
        c->pcb = NULL;
        c->len = 0;
    }
}

static err_t on_accept(void *arg, struct tcp_pcb *pcb, err_t err) {
    (void)arg;
    if (err != ERR_OK || pcb == NULL) {
        return ERR_VAL;
    }

    for (int i = 0; i < MODBUS_TCP_CONNS; i++) {
        if (conns[i].pcb != NULL) {
            continue;
        }
        conns[i].pcb = pcb;
        conns[i].len = 0;
        tcp_arg(pcb, &conns[i]);
        tcp_recv(pcb, on_recv);
        tcp_err(pcb, on_error);
        stat_connections++;
        return ERR_OK;
    }

    /* All slots busy: refuse now so the client sees a reset rather than a
     * connection that never answers. */
    tcp_abort(pcb);
    return ERR_ABRT;
}

bool modbus_tcp_init(void) {
    if (listener != NULL) {
        return true;
    }

    struct tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_ANY);
    if (pcb == NULL) {
        return false;
    }
    if (tcp_bind(pcb, IP_ANY_TYPE, MODBUS_TCP_PORT) != ERR_OK) {
        tcp_close(pcb);
        return false;
    }

    listener = tcp_listen_with_backlog(pcb, MODBUS_TCP_CONNS);
    if (listener == NULL) {
        tcp_close(pcb);
        return false;
    }
    tcp_accept(listener, on_accept);
    return true;
}
