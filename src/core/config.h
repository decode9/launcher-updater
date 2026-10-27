/*
 * config.h - launcher.json, the file shipped next to the executable (docs/spec.md):
 *
 *   {"server": "play.example.com", "port": 5103, "fingerprint": "<64 lowercase hex>",
 *    "blobs_url": null, "launch": {"exe": "Game.exe", "args": ["{server}"]}}
 *
 * server: host name or IPv4 (letters, digits, '.', '-'). port: 1-65535. fingerprint: SHA-256 of
 * the server certificate (DER). blobs_url: null/absent or an https:// URL. launch.exe: a safe
 * relative path (path.h). launch.args: optional array of strings. Unknown fields are ignored.
 */
#ifndef LAUNCHER_CORE_CONFIG_H
#define LAUNCHER_CORE_CONFIG_H

#include <stddef.h>
#include <stdint.h>

#include "json.h"
#include "path.h"
#include "sha256.h"

#define CONFIG_HOST_BYTES 254
#define CONFIG_URL_BYTES 512
#define CONFIG_ARGS_MAX 16
#define CONFIG_ARG_BYTES 256

typedef struct {
    char server[CONFIG_HOST_BYTES];
    uint16_t port;
    uint8_t fingerprint[SHA256_BYTES];
    int has_blobs_url;
    char blobs_url[CONFIG_URL_BYTES];
    char launch_exe[PATH_MAX_BYTES];
    uint32_t launch_argc;
    char launch_args[CONFIG_ARGS_MAX][CONFIG_ARG_BYTES];
} launcher_config;

typedef enum {
    CONFIG_OK = 0,
    CONFIG_ERR_JSON,
    CONFIG_ERR_SERVER,
    CONFIG_ERR_PORT,
    CONFIG_ERR_FINGERPRINT,
    CONFIG_ERR_BLOBS_URL,
    CONFIG_ERR_LAUNCH
} config_error;

/* tokens/cap: scratch token arena for the JSON parser (64 tokens are plenty). */
config_error config_parse(launcher_config *cfg, const char *text, size_t len, json_token *tokens, uint32_t cap);
const char *config_error_text(config_error error);

int config_host_is_valid(const char *host);
int config_https_url_is_valid(const char *url);

/* Splits a valid https URL into host, port (443 by default) and path prefix without a
 * trailing '/' ("" for none): "https://cdn.example.com:8443/game/blobs/" -> "cdn.example.com",
 * 8443, "/game/blobs". Returns 0 for anything else (bad host or port, query, fragment, user). */
int config_split_https_url(const char *url, char *host, size_t host_cap, unsigned *port, char *prefix,
                           size_t prefix_cap);

#endif
