/*
 * config.c - See config.h.
 */
#include "config.h"
#include "str.h"

int config_host_is_valid(const char *host)
{
    size_t i, n = str_len(host);
    if (n == 0 || n > 253 || host[0] == '.' || host[0] == '-') return 0;
    for (i = 0; i < n; i++) {
        char c = host[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' ||
              c == '-'))
            return 0;
    }
    return 1;
}

int config_https_url_is_valid(const char *url)
{
    const char *p;
    if (!str_starts_with(url, "https://") || url[8] == 0 || url[8] == '/') return 0;
    for (p = url; *p; p++)
        if ((unsigned char)*p <= 0x20 || *p == 0x7F || *p == '\\') return 0;
    return 1;
}

int config_split_https_url(const char *url, char *host, size_t host_cap, unsigned *port, char *prefix,
                           size_t prefix_cap)
{
    const char *p, *h;
    size_t host_len, n;
    uint64_t value = 443;
    if (!config_https_url_is_valid(url)) return 0;
    h = p = url + 8;
    while (*p && *p != ':' && *p != '/') p++;
    host_len = (size_t)(p - h);
    if (!str_copy_n(host, host_cap, h, host_len) || !config_host_is_valid(host)) return 0;
    if (*p == ':') {
        const char *digits = ++p;
        while (*p >= '0' && *p <= '9') p++;
        if (!dec_to_u64(digits, (size_t)(p - digits), &value) || value == 0 || value > 65535) return 0;
        if (*p && *p != '/') return 0;
    }
    *port = (unsigned)value;
    n = str_len(p);
    while (n && p[n - 1] == '/') n--;
    for (h = p; h < p + n; h++)
        if (*h == '?' || *h == '#' || *h == '@') return 0;
    return str_copy_n(prefix, prefix_cap, p, n);
}

static config_error parse_launch(launcher_config *cfg, const json_doc *doc, int launch)
{
    int args, a;
    if (json_kind(doc, launch) != JSON_OBJECT) return CONFIG_ERR_LAUNCH;
    if (!json_string(doc, json_get(doc, launch, "exe"), cfg->launch_exe, sizeof cfg->launch_exe, 0) ||
        !path_is_safe_relative(cfg->launch_exe))
        return CONFIG_ERR_LAUNCH;
    args = json_get(doc, launch, "args");
    if (args < 0 || json_is_null(doc, args)) return CONFIG_OK;
    if (json_kind(doc, args) != JSON_ARRAY || json_count(doc, args) > CONFIG_ARGS_MAX) return CONFIG_ERR_LAUNCH;
    for (a = json_first(doc, args); a >= 0; a = json_next(doc, a)) {
        if (!json_string(doc, a, cfg->launch_args[cfg->launch_argc], CONFIG_ARG_BYTES, 0)) return CONFIG_ERR_LAUNCH;
        cfg->launch_argc++;
    }
    return CONFIG_OK;
}

config_error config_parse(launcher_config *cfg, const char *text, size_t len, json_token *tokens, uint32_t cap)
{
    json_doc doc;
    char hex[SHA256_HEX + 1];
    uint64_t port;
    int blobs;
    size_t hex_len;

    cfg->server[0] = cfg->blobs_url[0] = cfg->launch_exe[0] = 0;
    cfg->port = 0;
    cfg->has_blobs_url = 0;
    cfg->launch_argc = 0;

    if (json_parse(&doc, text, len, tokens, cap) != JSON_OK || json_kind(&doc, 0) != JSON_OBJECT)
        return CONFIG_ERR_JSON;
    if (!json_string(&doc, json_get(&doc, 0, "server"), cfg->server, sizeof cfg->server, 0) ||
        !config_host_is_valid(cfg->server))
        return CONFIG_ERR_SERVER;
    if (!json_u64(&doc, json_get(&doc, 0, "port"), &port) || port == 0 || port > 65535) return CONFIG_ERR_PORT;
    cfg->port = (uint16_t)port;
    if (!json_string(&doc, json_get(&doc, 0, "fingerprint"), hex, sizeof hex, &hex_len) ||
        !hex_decode(hex, hex_len, cfg->fingerprint, SHA256_BYTES))
        return CONFIG_ERR_FINGERPRINT;
    blobs = json_get(&doc, 0, "blobs_url");
    if (blobs >= 0 && !json_is_null(&doc, blobs)) {
        if (!json_string(&doc, blobs, cfg->blobs_url, sizeof cfg->blobs_url, 0) ||
            !config_https_url_is_valid(cfg->blobs_url))
            return CONFIG_ERR_BLOBS_URL;
        cfg->has_blobs_url = 1;
    }
    return parse_launch(cfg, &doc, json_get(&doc, 0, "launch"));
}

const char *config_error_text(config_error error)
{
    switch (error) {
    case CONFIG_OK: return "ok";
    case CONFIG_ERR_JSON: return "launcher.json is not a valid JSON object";
    case CONFIG_ERR_SERVER: return "invalid or missing \"server\"";
    case CONFIG_ERR_PORT: return "invalid or missing \"port\"";
    case CONFIG_ERR_FINGERPRINT: return "\"fingerprint\" must be 64 lowercase hex digits";
    case CONFIG_ERR_BLOBS_URL: return "\"blobs_url\" must be null or an https:// URL";
    case CONFIG_ERR_LAUNCH: return "invalid \"launch\" (exe must be a safe relative path)";
    }
    return "unknown error";
}
