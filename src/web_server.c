#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/tcp.h"
#include "lwip/ip4_addr.h"
#include "lwip/netif.h"
#include "lwip/mem.h"
#include "dhcpserver.h"
#include "shared.h"

#define REQ_MAX 4096
#define DUMP_MAX 2048
#define RESP_MAX 4096

typedef struct {
    uint8_t req[REQ_MAX];
    size_t used, header_end, body_len;
    bool has_len, bad, responded;
    unsigned char action; /* 0=root/upload, 1=sense on, 2=sense off, 3=status */
    uint8_t response[RESP_MAX];
    size_t response_len, response_queued;
} http_client_t;

static dhcp_server_t dhcp;
static struct tcp_pcb *listener;

static const char page[] =
"<!doctype html><html lang='pt-BR'><meta charset='utf-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>PTPE Skylanders</title><style>body{font:16px system-ui;max-width:680px;margin:2em auto;padding:0 1em;background:#101522;color:#eee}button{padding:12px 15px;background:#54d6a0;border:0;border-radius:8px;margin:4px;font-weight:600}button.secondary{background:#38445c;color:#fff}input{margin:1em 0}#s{white-space:pre-wrap;background:#1b2435;border-radius:8px;padding:12px}.group{border:1px solid #354158;border-radius:12px;padding:14px;margin:14px 0}small{color:#aebbd0}</style>"
"<h1>PTPE Skylanders</h1><p>Portal virtual e carregamento de dump para o Pico 2 W.</p>"
"<section class='group'><h2>Controles virtuais</h2><p>Simula a presença de um Skylander na resposta Sense do portal.</p><button id=on>Simular Skylander presente</button><button class=secondary id=off>Remover Skylander</button><button class=secondary id=status>Atualizar status</button></section>"
"<section class='group'><h2>Carregar Skylander</h2><p>Selecione um dump .bin (16 a 2048 bytes; tamanho múltiplo de 16).</p><input id=f type=file accept='.bin'><br><button id=b>Enviar dump</button></section><p id=s>Pronto. Conecte-se ao Wi-Fi PTPE-Skylanders e abra 192.168.4.1.</p>"
"<script>let f=document.querySelector('#f'),s=document.querySelector('#s');async function action(path){s.textContent='Enviando comando...';try{let r=await fetch(path,{cache:'no-store'});s.textContent=await r.text()}catch(e){s.textContent='Falha: '+e}}document.querySelector('#on').onclick=()=>action('/sense/on');document.querySelector('#off').onclick=()=>action('/sense/off');document.querySelector('#status').onclick=()=>action('/status');document.querySelector('#b').onclick=async()=>{let x=f.files[0];if(!x){s.textContent='Selecione um arquivo .bin.';return}if(!x.size||x.size>2048||x.size%16!==0){s.textContent='Tamanho inválido: use 16–2048 bytes, múltiplo de 16.';return}s.textContent='Enviando dump...';try{let r=await fetch('/upload',{method:'POST',headers:{'Content-Type':'application/octet-stream'},body:x});s.textContent=await r.text()}catch(e){s.textContent='Falha: '+e}}</script>";

static void cleanup(struct tcp_pcb *pcb, http_client_t *c, bool abort_conn) {
    if (pcb) {
        tcp_arg(pcb, NULL);
        tcp_recv(pcb, NULL);
        tcp_sent(pcb, NULL);
        tcp_poll(pcb, NULL, 0);
        tcp_err(pcb, NULL);
        if (abort_conn) tcp_abort(pcb);
        else if (tcp_close(pcb) != ERR_OK) tcp_abort(pcb);
    }
    if (c) free(c);
}

static err_t flush_response(struct tcp_pcb *pcb, http_client_t *c) {
    while (c->response_queued < c->response_len) {
        u16_t available = tcp_sndbuf(pcb);
        if (available == 0) break;
        size_t remaining = c->response_len - c->response_queued;
        size_t chunk = remaining;
        if (chunk > available) chunk = available;
        if (chunk > 1024) chunk = 1024;

        err_t e = tcp_write(pcb, c->response + c->response_queued,
                            (u16_t)chunk, TCP_WRITE_FLAG_COPY);
        if (e == ERR_MEM) break; /* tcp_sent_cb retries when ACKs free space */
        if (e != ERR_OK) return e;
        c->response_queued += chunk;
    }

    err_t out = tcp_output(pcb);
    if (out != ERR_OK && out != ERR_MEM) return out;

    if (c->response_queued == c->response_len) {
        /* All bytes are queued; TCP will transmit them before FIN. */
        tcp_arg(pcb, NULL);
        tcp_recv(pcb, NULL);
        tcp_sent(pcb, NULL);
        tcp_poll(pcb, NULL, 0);
        tcp_err(pcb, NULL);
        err_t e = tcp_close(pcb);
        if (e != ERR_OK) {
            tcp_abort(pcb);
        }
        free(c);
    }
    return ERR_OK;
}

static err_t sent_http(void *arg, struct tcp_pcb *pcb, u16_t len) {
    (void)len;
    http_client_t *c = (http_client_t *)arg;
    if (!c) return ERR_OK;
    err_t e = flush_response(pcb, c);
    if (e != ERR_OK) {
        cleanup(pcb, c, true);
        return ERR_ABRT;
    }
    return ERR_OK;
}

static err_t poll_http(void *arg, struct tcp_pcb *pcb) {
    http_client_t *c = (http_client_t *)arg;
    if (!c) return ERR_OK;
    err_t e = flush_response(pcb, c);
    if (e != ERR_OK) {
        cleanup(pcb, c, true);
        return ERR_ABRT;
    }
    return ERR_OK;
}

static err_t respond(struct tcp_pcb *pcb, http_client_t *c,
                     const char *status, const char *type,
                     const void *body, size_t body_len) {
    int h = snprintf((char *)c->response, sizeof(c->response),
        "HTTP/1.1 %s\\r\\nContent-Type: %s\\r\\nContent-Length: %lu\\r\\n"
        "Connection: close\\r\\nCache-Control: no-store\\r\\n\\r\\n",
        status, type, (unsigned long)body_len);
    if (h < 0 || (size_t)h >= sizeof(c->response) ||
        body_len > sizeof(c->response) - (size_t)h) return ERR_VAL;
    if (body_len && body) memcpy(c->response + h, body, body_len);
    c->response_len = (size_t)h + body_len;
    c->response_queued = 0;
    c->responded = true;

    tcp_sent(pcb, sent_http);
    tcp_poll(pcb, poll_http, 2);
    return flush_response(pcb, c);
}

static bool eq_ci(const char *a, size_t n, const char *b) {
    size_t m = strlen(b);
    if (n != m) return false;
    for (size_t i = 0; i < n; i++)
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return false;
    return true;
}

static void parse_request(http_client_t *c) {
    char *s = (char *)c->req;
    char *line_end = strstr(s, "\r\n");
    if (!line_end) { c->bad = true; return; }
    size_t req_len = (size_t)(line_end - s);
    bool get_root = req_len >= 6 && memcmp(s, "GET / ", 6) == 0;
    bool get_sense_on = req_len >= 14 && memcmp(s, "GET /sense/on ", 14) == 0;
    bool get_sense_off = req_len >= 15 && memcmp(s, "GET /sense/off ", 15) == 0;
    bool get_status = req_len >= 12 && memcmp(s, "GET /status ", 12) == 0;
    bool post_upload = req_len >= 13 && memcmp(s, "POST /upload ", 13) == 0;
    if (get_root || get_sense_on || get_sense_off || get_status) {
        c->body_len = (size_t)-1;
        c->action = get_sense_on ? 1 : get_sense_off ? 2 : get_status ? 3 : 0;
        return;
    }
    if (!post_upload) { c->bad = true; return; }

    char *line = line_end + 2;
    char *limit = (char *)c->req + c->header_end - 2;
    while (line < limit) {
        char *end = strstr(line, "\r\n");
        if (!end || end > limit) { c->bad = true; return; }
        char *colon = memchr(line, ':', (size_t)(end - line));
        if (colon && eq_ci(line, (size_t)(colon-line), "Content-Length")) {
            char *v = colon + 1;
            while (v < end && (*v == ' ' || *v == '\t')) v++;
            if (v == end || !isdigit((unsigned char)*v) || c->has_len) { c->bad = true; return; }
            unsigned long n = 0;
            while (v < end && isdigit((unsigned char)*v)) {
                n = n * 10UL + (unsigned long)(*v - '0');
                if (n > DUMP_MAX) { c->bad = true; return; }
                v++;
            }
            while (v < end && (*v == ' ' || *v == '\t')) v++;
            if (v != end || n < 16 || n % 16 != 0) { c->bad = true; return; }
            c->body_len = (size_t)n;
            c->has_len = true;
        }
        line = end + 2;
    }
    if (!c->has_len) c->bad = true;
}

static err_t recv_http(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err) {
    http_client_t *c = (http_client_t *)arg;
    if (!c) { if (p) pbuf_free(p); return ERR_ARG; }
    if (err != ERR_OK) { if (p) pbuf_free(p); cleanup(pcb, c, true); return err; }
    if (!p) { cleanup(pcb, c, false); return ERR_OK; }
    tcp_recved(pcb, p->tot_len);

    if ((size_t)p->tot_len > sizeof(c->req) - c->used) {
        pbuf_free(p);
        static const char msg[] = "Request too large.";
        if (respond(pcb, c, "413 Payload Too Large", "text/plain", msg, sizeof(msg)-1) != ERR_OK)
            cleanup(pcb, c, true);
        return ERR_OK;
    }
    pbuf_copy_partial(p, c->req + c->used, p->tot_len, 0);
    c->used += p->tot_len;
    pbuf_free(p);
    if (c->responded) return ERR_OK;

    if (!c->header_end) {
        for (size_t i = 3; i < c->used; i++) {
            if (c->req[i-3]=='\r' && c->req[i-2]=='\n' &&
                c->req[i-1]=='\r' && c->req[i]=='\n') {
                c->header_end = i + 1;
                break;
            }
        }
        if (!c->header_end) {
            if (c->used == sizeof(c->req)) {
                static const char msg[] = "HTTP headers too large.";
                if (respond(pcb, c, "431 Request Header Fields Too Large", "text/plain", msg, sizeof(msg)-1) != ERR_OK)
                    cleanup(pcb, c, true);
            }
            return ERR_OK;
        }
        /* Terminate header text at the start of the final CRLFCRLF. */
        c->req[c->header_end - 2] = '\0';
        parse_request(c);
        if (c->bad) {
            static const char msg[] = "Invalid request or Content-Length.";
            if (respond(pcb, c, "400 Bad Request", "text/plain", msg, sizeof(msg)-1) != ERR_OK)
                cleanup(pcb, c, true);
            return ERR_OK;
        }
        if (c->body_len == (size_t)-1) {
            if (c->action == 1) {
                web_sense_present = 1;
                static const char msg[] = "OK: Sense virtual ativado; o proximo comando S do Xbox recebera bitmask 0x01.";
                if (respond(pcb, c, "200 OK", "text/plain; charset=utf-8", msg, sizeof(msg)-1) != ERR_OK) cleanup(pcb, c, true);
            } else if (c->action == 2) {
                web_sense_present = 0;
                static const char msg[] = "OK: Sense virtual desativado.";
                if (respond(pcb, c, "200 OK", "text/plain; charset=utf-8", msg, sizeof(msg)-1) != ERR_OK) cleanup(pcb, c, true);
            } else if (c->action == 3) {
                char msg[128];
                int n = snprintf(msg, sizeof(msg), "PTPE status\\nSense virtual: %s\\nDump RAM: %s (%u bytes)", web_sense_present ? "PRESENTE" : "ausente", skylander_ram_dump_active ? "ativo" : "inativo", skylander_ram_dump_size);
                if (n < 0) n = 0;
                if ((size_t)n >= sizeof(msg)) n = (int)sizeof(msg)-1;
                if (respond(pcb, c, "200 OK", "text/plain; charset=utf-8", msg, (size_t)n) != ERR_OK) cleanup(pcb, c, true);
            } else {
                if (respond(pcb, c, "200 OK", "text/html; charset=utf-8", page, sizeof(page)-1) != ERR_OK)
                    cleanup(pcb, c, true);
            }
            return ERR_OK;
        }
    }

    if (c->header_end && c->has_len) {
        size_t got = c->used - c->header_end;
        if (got > c->body_len) {
            static const char msg[] = "Invalid upload length.";
            if (respond(pcb, c, "400 Bad Request", "text/plain", msg, sizeof(msg)-1) != ERR_OK)
                cleanup(pcb, c, true);
            return ERR_OK;
        }
        if (got == c->body_len) {
            memcpy(skylander_ram_dump, c->req + c->header_end, c->body_len);
            skylander_ram_dump_size = (unsigned int)c->body_len;
            skylander_ram_dump_active = 1;

            char msg[80];
            int n = snprintf(msg, sizeof(msg), "Dump active in RAM (%lu bytes).", (unsigned long)c->body_len);
            if (n < 0) n = 0;
            if ((size_t)n >= sizeof(msg)) n = (int)sizeof(msg)-1;
            if (respond(pcb, c, "200 OK", "text/plain; charset=utf-8", msg, (size_t)n) != ERR_OK)
                cleanup(pcb, c, true);
        }
    }
    return ERR_OK;
}

static void err_http(void *arg, err_t err) {
    (void)err;
    if (arg) free(arg);
}

static err_t accept_http(void *arg, struct tcp_pcb *pcb, err_t err) {
    (void)arg;
    if (err != ERR_OK || !pcb) return ERR_VAL;
    http_client_t *c = (http_client_t *)calloc(1, sizeof(*c));
    if (!c) { tcp_abort(pcb); return ERR_ABRT; }
    tcp_arg(pcb, c);
    tcp_recv(pcb, recv_http);
    tcp_err(pcb, err_http);
    return ERR_OK;
}

void ptpe_web_start(void) {
    ip4_addr_t ip, mask, gw;
    IP4_ADDR(&ip, 192, 168, 4, 1);
    IP4_ADDR(&mask, 255, 255, 255, 0);
    IP4_ADDR(&gw, 192, 168, 4, 1);
    cyw43_arch_enable_ap_mode("PTPE-Skylanders", "SkyPortal26", CYW43_AUTH_WPA2_AES_PSK);
    netif_set_addr(&cyw43_state.netif[CYW43_ITF_AP], &ip, &mask, &gw);
    dhcp_server_init(&dhcp, &cyw43_state.netif[CYW43_ITF_AP], &gw, &mask);

    struct tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_V4);
    if (!pcb) { printf("PTPE web: tcp_new falhou\n"); return; }
    if (tcp_bind(pcb, IP_ADDR_ANY, 80) != ERR_OK) {
        printf("PTPE web: tcp_bind falhou\n");
        tcp_close(pcb);
        return;
    }
    listener = tcp_listen_with_backlog(pcb, 2);
    if (!listener) { printf("PTPE web: listen falhou\n"); return; }
    tcp_accept(listener, accept_http);
    printf("Wi-Fi PTPE-Skylanders / password SkyPortal26 / http://192.168.4.1\n");
}
