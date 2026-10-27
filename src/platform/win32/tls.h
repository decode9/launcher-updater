/*
 * tls.h - Blocking TLS 1.2 client over Winsock + Schannel with certificate pinning.
 *
 * pin == 0 (D-11, blobs_url hosts): Schannel validates the chain and host name instead.
 * The server is checked only by the pin: SHA-256 of its DER certificate. No chain validation
 * (Wine's Schannel does not validate chains and negotiates TLS 1.2 only, so the client asks
 * for exactly 1.2). Every socket wait is bounded by TLS_TIMEOUT_MS (connect, send, recv).
 * A connection keeps its buffers inline (~53 KiB): give it static storage, never the stack.
 */
#ifndef LAUNCHER_TLS_H
#define LAUNCHER_TLS_H

#include "core/sha256.h"
#include "win32.h"

#define TLS_TIMEOUT_MS 15000
#define TLS_RX_BYTES 0x8000
#define TLS_TX_BYTES 0x1000
#define TLS_PLAIN_BYTES 0x4400
#define TLS_HOST_CHARS 256

typedef enum {
    TLS_OK = 0,
    TLS_ERR_RESOLVE,
    TLS_ERR_CONNECT,
    TLS_ERR_HANDSHAKE,
    TLS_ERR_PIN,
    TLS_ERR_IO,     /* send/recv failed, timed out or the peer closed mid-record */
    TLS_ERR_SIZE
} tls_error;

typedef struct {
    DWORD socket;
    DWORD cred[2], ctx[2];     /* SSPI CredHandle / CtxtHandle */
    int has_wsa, has_cred, has_ctx;
    int eof;                   /* peer closed (close_notify or TCP FIN at a record boundary) */
    DWORD header, trailer;     /* SecPkgContext_StreamSizes */
    DWORD rx_len;              /* encrypted bytes waiting in rx */
    DWORD plain_pos, plain_len;
    wchar_t target[TLS_HOST_CHARS]; /* SNI name */
    u8 rx[TLS_RX_BYTES];
    u8 tx[TLS_TX_BYTES];
    u8 plain[TLS_PLAIN_BYTES];
} tls_conn;

tls_error tls_open(tls_conn *conn, const char *host, unsigned port, const uint8_t pin[SHA256_BYTES]);
tls_error tls_send(tls_conn *conn, const u8 *data, int size); /* size <= TLS_TX_BYTES - overhead */

/* Reads up to size bytes: returns the count (> 0), 0 once the peer has closed, -1 on error. */
int tls_recv(tls_conn *conn, u8 *data, int size);

void tls_close(tls_conn *conn); /* safe on a half-open connection */
const char *tls_error_text(tls_error error);

#endif
