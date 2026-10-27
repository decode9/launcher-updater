/*
 * tls.c - Winsock + Schannel TLS client (see tls.h). The SSPI and Winsock declarations stay
 * private to this file.
 */
#include "tls.h"
#include "rt.h"

/* ---- ws2_32 -------------------------------------------------------------- */

typedef DWORD SOCKET;
struct sockaddr_in { short family; u16 port; DWORD addr; char zero[8]; };
struct hostent { char *name; char **aliases; short addrtype; short length; char **addr_list; };
typedef struct { unsigned count; SOCKET fds[64]; } fd_set;
struct timeval { long sec, usec; };

IMPORT int WINAPI WSAStartup(u16, void *);
IMPORT int WINAPI WSACleanup(void);
IMPORT SOCKET WINAPI socket(int, int, int);
IMPORT int WINAPI connect(SOCKET, const void *, int);
IMPORT int WINAPI send(SOCKET, const char *, int, int);
IMPORT int WINAPI recv(SOCKET, char *, int, int);
IMPORT int WINAPI closesocket(SOCKET);
IMPORT int WINAPI setsockopt(SOCKET, int, int, const void *, int);
IMPORT int WINAPI getsockopt(SOCKET, int, int, void *, int *);
IMPORT int WINAPI ioctlsocket(SOCKET, long, DWORD *);
IMPORT int WINAPI select(int, fd_set *, fd_set *, fd_set *, const struct timeval *);
IMPORT DWORD WINAPI inet_addr(const char *);
IMPORT struct hostent *WINAPI gethostbyname(const char *);

#define INVALID_SOCKET ((SOCKET)~0UL)
#define INADDR_NONE 0xffffffffUL
#define AF_INET 2
#define SOCK_STREAM 1
#define IPPROTO_TCP 6
#define SOL_SOCKET 0xffff
#define SO_SNDTIMEO 0x1005
#define SO_RCVTIMEO 0x1006
#define SO_ERROR 0x1007
#define FIONBIO ((long)0x8004667EUL)

/* ---- secur32 / crypt32 --------------------------------------------------- */

typedef long SECURITY_STATUS;
typedef struct { DWORD low; long high; } TimeStamp;
typedef struct { DWORD size; DWORD type; void *data; } SecBuffer;
typedef struct { DWORD version; DWORD count; SecBuffer *buffers; } SecBufferDesc;
typedef struct {
    DWORD dwVersion, cCreds; void *paCred; HANDLE hRootStore;
    DWORD cMappers; void *aphMappers; DWORD cSupportedAlgs; void *palgSupportedAlgs;
    DWORD grbitEnabledProtocols, dwMinimumCipherStrength, dwMaximumCipherStrength;
    DWORD dwSessionLifespan, dwFlags, dwCredFormat;
} SCHANNEL_CRED;
typedef struct { DWORD header, trailer, max_message, buffers, block_size; } StreamSizes;
typedef struct { DWORD encoding; u8 *der; DWORD der_size; void *info; HANDLE store; } CERT_CONTEXT;

IMPORT SECURITY_STATUS WINAPI AcquireCredentialsHandleW(const wchar_t *, const wchar_t *, DWORD, void *, void *, void *, void *, DWORD *, TimeStamp *);
IMPORT SECURITY_STATUS WINAPI InitializeSecurityContextW(DWORD *, DWORD *, const wchar_t *, DWORD, DWORD, DWORD, SecBufferDesc *, DWORD, DWORD *, SecBufferDesc *, DWORD *, TimeStamp *);
IMPORT SECURITY_STATUS WINAPI QueryContextAttributesW(DWORD *, DWORD, void *);
IMPORT SECURITY_STATUS WINAPI EncryptMessage(DWORD *, DWORD, SecBufferDesc *, DWORD);
IMPORT SECURITY_STATUS WINAPI DecryptMessage(DWORD *, SecBufferDesc *, DWORD, DWORD *);
IMPORT SECURITY_STATUS WINAPI FreeContextBuffer(void *);
IMPORT SECURITY_STATUS WINAPI DeleteSecurityContext(DWORD *);
IMPORT SECURITY_STATUS WINAPI FreeCredentialsHandle(DWORD *);
IMPORT BOOL WINAPI CertFreeCertificateContext(const CERT_CONTEXT *);

#define SECPKG_CRED_OUTBOUND 2
#define SCHANNEL_CRED_VERSION 4
#define SP_PROT_TLS1_2_CLIENT 0x00000800 /* Wine: 1.2 only */
#define SCH_CRED_MANUAL_CRED_VALIDATION 0x00000008
#define SCH_CRED_NO_DEFAULT_CREDS 0x00000010
#define ISC_FLAGS (0x4 | 0x8 | 0x10 | 0x100 | 0x4000 | 0x8000) /* replay, sequence, confidentiality,
                                                                  allocate memory, extended error, stream */
#define SECBUFFER_EMPTY 0
#define SECBUFFER_DATA 1
#define SECBUFFER_TOKEN 2
#define SECBUFFER_EXTRA 5
#define SECBUFFER_STREAM_TRAILER 6
#define SECBUFFER_STREAM_HEADER 7
#define SEC_E_OK 0
#define SEC_I_CONTINUE_NEEDED 0x00090312
#define SEC_E_INCOMPLETE_MESSAGE ((long)0x80090318)
#define SEC_I_CONTEXT_EXPIRED 0x00090317
#define SECPKG_ATTR_STREAM_SIZES 4
#define SECPKG_ATTR_REMOTE_CERT_CONTEXT 0x53

static const wchar_t UNISP_NAME[] = L"Microsoft Unified Security Protocol Provider";

/* ---- socket -------------------------------------------------------------- */

static int send_all(SOCKET s, const u8 *data, DWORD size)
{
    while (size > 0) {
        int n = send(s, (const char *)data, (int)size, 0);
        if (n <= 0) return 0;
        data += n;
        size -= (DWORD)n;
    }
    return 1;
}

static DWORD resolve(const char *host)
{
    DWORD addr = inet_addr(host);
    struct hostent *entry;
    if (addr != INADDR_NONE) return addr;
    entry = gethostbyname(host);
    if (!entry || entry->length != 4 || !entry->addr_list || !entry->addr_list[0]) return INADDR_NONE;
    return *(DWORD *)entry->addr_list[0];
}

static void set_timeouts(SOCKET s)
{
    DWORD ms = TLS_TIMEOUT_MS;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &ms, sizeof ms);
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &ms, sizeof ms);
}

/* Non-blocking connect + select, so an unreachable host costs at most TLS_TIMEOUT_MS. */
static int connect_bounded(SOCKET s, DWORD addr, unsigned port)
{
    struct sockaddr_in sa = {AF_INET, (u16)((port >> 8 & 0xff) | (port & 0xff) << 8), addr, {0}};
    struct timeval wait = {TLS_TIMEOUT_MS / 1000, 0};
    fd_set writable = {1, {s}}, failed = {1, {s}};
    DWORD mode = 1;
    int error = 1, size = sizeof error;
    if (ioctlsocket(s, FIONBIO, &mode)) return 0;
    connect(s, &sa, sizeof sa); /* WSAEWOULDBLOCK expected: select decides */
    if (select(0, 0, &writable, &failed, &wait) <= 0 || failed.count || !writable.count) return 0;
    if (getsockopt(s, SOL_SOCKET, SO_ERROR, &error, &size) || error) return 0;
    mode = 0;
    return ioctlsocket(s, FIONBIO, &mode) == 0;
}

/* ---- handshake ----------------------------------------------------------- */

#define SCH_CRED_AUTO_CRED_VALIDATION 0x00000020

/* With a pin the certificate is checked by hand (pin_matches); without one Schannel validates
 * the chain and the name against the system trust store (D-11: blobs_url hosts). */
static int acquire_credentials(tls_conn *c, int pinned)
{
    SCHANNEL_CRED sc = {0};
    TimeStamp expiry;
    sc.dwVersion = SCHANNEL_CRED_VERSION;
    sc.grbitEnabledProtocols = SP_PROT_TLS1_2_CLIENT;
    sc.dwFlags = SCH_CRED_NO_DEFAULT_CREDS | (pinned ? SCH_CRED_MANUAL_CRED_VALIDATION : SCH_CRED_AUTO_CRED_VALIDATION);
    if (AcquireCredentialsHandleW(0, UNISP_NAME, SECPKG_CRED_OUTBOUND, 0, &sc, 0, 0, c->cred, &expiry))
        return 0;
    c->has_cred = 1;
    return 1;
}

/* Sends an ISC output token (if any) and frees it. */
static int flush_token(tls_conn *c, SecBuffer *token)
{
    int ok = 1;
    if (token->size && token->data) {
        ok = send_all(c->socket, token->data, token->size);
        FreeContextBuffer(token->data);
    }
    token->data = 0;
    token->size = 0;
    return ok;
}

static int handshake_start(tls_conn *c)
{
    SecBuffer out = {0, SECBUFFER_TOKEN, 0};
    SecBufferDesc out_desc = {0, 1, &out};
    DWORD attrs;
    TimeStamp expiry;
    SECURITY_STATUS ss = InitializeSecurityContextW(c->cred, 0, c->target, ISC_FLAGS, 0, 0, 0, 0,
                                                    c->ctx, &out_desc, &attrs, &expiry);
    if (ss != SEC_I_CONTINUE_NEEDED) return 0;
    c->has_ctx = 1;
    return flush_token(c, &out);
}

/* 1: got bytes; 0: the peer closed the TCP stream; -1: error, timeout or rx full. */
static int receive_more(tls_conn *c)
{
    int n;
    if (c->rx_len >= TLS_RX_BYTES) return -1;
    n = recv(c->socket, (char *)c->rx + c->rx_len, (int)(TLS_RX_BYTES - c->rx_len), 0);
    if (n < 0) return -1;
    if (n == 0) return 0;
    c->rx_len += (DWORD)n;
    return 1;
}

/* Keeps only the unconsumed tail (SECBUFFER_EXTRA) of rx. */
static void keep_extra(tls_conn *c, const SecBuffer *extra)
{
    if (extra && extra->size) {
        memmove(c->rx, c->rx + c->rx_len - extra->size, extra->size);
        c->rx_len = extra->size;
    } else {
        c->rx_len = 0;
    }
}

static int handshake_loop(tls_conn *c)
{
    int need_recv = 1;
    for (;;) {
        SecBuffer in[2] = {{0, SECBUFFER_TOKEN, 0}, {0, SECBUFFER_EMPTY, 0}};
        SecBuffer out = {0, SECBUFFER_TOKEN, 0};
        SecBufferDesc in_desc = {0, 2, in}, out_desc = {0, 1, &out};
        DWORD attrs;
        TimeStamp expiry;
        SECURITY_STATUS ss;
        if (need_recv && receive_more(c) <= 0) return 0;
        in[0].data = c->rx;
        in[0].size = c->rx_len;
        ss = InitializeSecurityContextW(c->cred, c->ctx, c->target, ISC_FLAGS, 0, 0, &in_desc, 0, 0,
                                        &out_desc, &attrs, &expiry);
        if (ss == SEC_E_INCOMPLETE_MESSAGE) { need_recv = 1; continue; }
        if (!flush_token(c, &out)) return 0;
        if (ss != SEC_E_OK && ss != SEC_I_CONTINUE_NEEDED) return 0;
        keep_extra(c, in[1].type == SECBUFFER_EXTRA ? &in[1] : 0);
        if (ss == SEC_E_OK) return 1;
        need_recv = c->rx_len == 0;
    }
}

/* Hashes the DER bytes directly (Wine lacks CERT_SHA256_HASH_PROP_ID). Constant-time compare. */
static int pin_matches(tls_conn *c, const uint8_t pin[SHA256_BYTES])
{
    const CERT_CONTEXT *cert = 0;
    uint8_t digest[SHA256_BYTES];
    unsigned diff = 0;
    int i;
    if (QueryContextAttributesW(c->ctx, SECPKG_ATTR_REMOTE_CERT_CONTEXT, &cert) || !cert) return 0;
    sha256(cert->der, cert->der_size, digest);
    CertFreeCertificateContext(cert);
    for (i = 0; i < SHA256_BYTES; i++) diff |= (unsigned)(digest[i] ^ pin[i]);
    return diff == 0;
}

static int read_stream_sizes(tls_conn *c)
{
    StreamSizes sizes;
    if (QueryContextAttributesW(c->ctx, SECPKG_ATTR_STREAM_SIZES, &sizes)) return 0;
    c->header = sizes.header;
    c->trailer = sizes.trailer;
    return 1;
}

/* SNI: the host as given (ASCII, validated by the caller's config). */
static int set_target(tls_conn *c, const char *host)
{
    int i;
    for (i = 0; host[i]; i++) {
        if (i + 1 >= TLS_HOST_CHARS || (unsigned char)host[i] >= 0x80) return 0;
        c->target[i] = (wchar_t)host[i];
    }
    c->target[i] = 0;
    return 1;
}

tls_error tls_open(tls_conn *conn, const char *host, unsigned port, const uint8_t pin[SHA256_BYTES])
{
    static u8 wsa_data[512]; /* WSADATA, never read */
    DWORD addr;
    memset(conn, 0, offsetof(tls_conn, rx)); /* state only; the buffers are overwritten */
    conn->socket = INVALID_SOCKET;
    if (!set_target(conn, host)) return TLS_ERR_RESOLVE;
    if (WSAStartup(0x0202, wsa_data)) return TLS_ERR_CONNECT;
    conn->has_wsa = 1;
    addr = resolve(host);
    if (addr == INADDR_NONE) return TLS_ERR_RESOLVE;
    conn->socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (conn->socket == INVALID_SOCKET || !connect_bounded(conn->socket, addr, port)) return TLS_ERR_CONNECT;
    set_timeouts(conn->socket);
    if (!acquire_credentials(conn, pin != 0) || !handshake_start(conn) || !handshake_loop(conn)) return TLS_ERR_HANDSHAKE;
    if (pin && !pin_matches(conn, pin)) return TLS_ERR_PIN;
    return read_stream_sizes(conn) ? TLS_OK : TLS_ERR_HANDSHAKE;
}

/* ---- application data ---------------------------------------------------- */

tls_error tls_send(tls_conn *c, const u8 *data, int size)
{
    SecBuffer parts[4];
    SecBufferDesc desc = {0, 4, parts};
    u8 *body = c->tx + c->header;
    if (size <= 0 || c->header + (DWORD)size + c->trailer > TLS_TX_BYTES) return TLS_ERR_SIZE;
    memcpy(body, data, (size_t)size);
    parts[0] = (SecBuffer){c->header, SECBUFFER_STREAM_HEADER, c->tx};
    parts[1] = (SecBuffer){(DWORD)size, SECBUFFER_DATA, body};
    parts[2] = (SecBuffer){c->trailer, SECBUFFER_STREAM_TRAILER, body + size};
    parts[3] = (SecBuffer){0, SECBUFFER_EMPTY, 0};
    if (EncryptMessage(c->ctx, 0, &desc, 0)) return TLS_ERR_IO;
    return send_all(c->socket, c->tx, parts[0].size + parts[1].size + parts[2].size) ? TLS_OK : TLS_ERR_IO;
}

/* Decrypts records until one carries data: 1 data, 0 orderly close, -1 error. */
static int read_record(tls_conn *c)
{
    if (c->eof) return 0;
    for (;;) {
        SecBuffer parts[4];
        SecBufferDesc desc = {0, 4, parts};
        SecBuffer *data = 0, *extra = 0;
        DWORD quality = 0;
        SECURITY_STATUS ss = SEC_E_INCOMPLETE_MESSAGE;
        int i;
        if (c->rx_len) {
            parts[0] = (SecBuffer){c->rx_len, SECBUFFER_DATA, c->rx};
            for (i = 1; i < 4; i++) parts[i] = (SecBuffer){0, SECBUFFER_EMPTY, 0};
            ss = DecryptMessage(c->ctx, &desc, 0, &quality);
        }
        if (ss == SEC_E_INCOMPLETE_MESSAGE) {
            int got = receive_more(c);
            if (got < 0) return -1;
            if (got == 0) {
                if (c->rx_len) return -1; /* closed in the middle of a record */
                c->eof = 1;
                return 0;
            }
            continue;
        }
        if (ss == SEC_I_CONTEXT_EXPIRED) { /* close_notify */
            c->eof = 1;
            return 0;
        }
        if (ss != SEC_E_OK) return -1;
        for (i = 1; i < 4; i++) {
            if (parts[i].type == SECBUFFER_DATA) data = &parts[i];
            if (parts[i].type == SECBUFFER_EXTRA) extra = &parts[i];
        }
        if (data && data->size > TLS_PLAIN_BYTES) return -1;
        if (data && data->size) memcpy(c->plain, data->data, data->size);
        c->plain_pos = 0;
        c->plain_len = data ? data->size : 0;
        keep_extra(c, extra);
        if (c->plain_len) return 1;
    }
}

int tls_recv(tls_conn *c, u8 *data, int size)
{
    DWORD n;
    if (size <= 0) return -1;
    if (c->plain_pos == c->plain_len) {
        int r = read_record(c);
        if (r <= 0) return r;
    }
    n = c->plain_len - c->plain_pos;
    if (n > (DWORD)size) n = (DWORD)size;
    memcpy(data, c->plain + c->plain_pos, n);
    c->plain_pos += n;
    return (int)n;
}

void tls_close(tls_conn *c)
{
    if (c->has_ctx) DeleteSecurityContext(c->ctx);
    if (c->has_cred) FreeCredentialsHandle(c->cred);
    if (c->socket != INVALID_SOCKET) closesocket(c->socket);
    if (c->has_wsa) WSACleanup();
    c->has_ctx = c->has_cred = c->has_wsa = 0;
    c->socket = INVALID_SOCKET;
    memset(c->plain, 0, sizeof c->plain); /* drop decrypted bytes */
}

const char *tls_error_text(tls_error error)
{
    switch (error) {
    case TLS_OK: return "ok";
    case TLS_ERR_RESOLVE: return "cannot resolve host";
    case TLS_ERR_CONNECT: return "cannot connect";
    case TLS_ERR_HANDSHAKE: return "TLS handshake failed";
    case TLS_ERR_PIN: return "server certificate does not match the pinned fingerprint";
    case TLS_ERR_IO: return "connection failed";
    case TLS_ERR_SIZE: return "message too large";
    }
    return "unknown error";
}
