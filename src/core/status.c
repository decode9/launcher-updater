/*
 * status.c - See status.h.
 */
#include "status.h"
#include "config.h"
#include "str.h"
#include "version.h"

/* ---- time ---------------------------------------------------------------- */

static int digits(const char *s, int n, int *out)
{
    int v = 0, i;
    for (i = 0; i < n; i++) {
        if (s[i] < '0' || s[i] > '9') return 0;
        v = v * 10 + (s[i] - '0');
    }
    *out = v;
    return 1;
}

static int days_in_month(int y, int m)
{
    static const int days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
    return m == 2 && leap ? 29 : days[m - 1];
}

/* Days since 1970-01-01 (H. Hinnant's days_from_civil, years >= 1970 so all ints stay positive). */
static int32_t days_from_civil(int y, int m, int d)
{
    int era, yoe, doy, doe;
    y -= m <= 2;
    era = y / 400;
    yoe = y - era * 400;
    doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

int utc_parse(const char *s, size_t len, int64_t *out)
{
    int y, mo, d, h, mi, sec;
    size_t i = 19;
    if (len < 20 || s[4] != '-' || s[7] != '-' || s[10] != 'T' || s[13] != ':' || s[16] != ':') return 0;
    if (!digits(s, 4, &y) || !digits(s + 5, 2, &mo) || !digits(s + 8, 2, &d) || !digits(s + 11, 2, &h) ||
        !digits(s + 14, 2, &mi) || !digits(s + 17, 2, &sec))
        return 0;
    if (s[i] == '.') {
        i++;
        if (i >= len || s[i] < '0' || s[i] > '9') return 0;
        while (i < len && s[i] >= '0' && s[i] <= '9') i++;
    }
    if (i + 1 != len || s[i] != 'Z') return 0;
    if (y < 1970 || mo < 1 || mo > 12 || d < 1 || d > days_in_month(y, mo) || h > 23 || mi > 59 || sec > 60)
        return 0;
    *out = (int64_t)days_from_civil(y, mo, d) * 86400 + h * 3600 + mi * 60 + sec;
    return 1;
}

static int read_time(const json_doc *doc, int i, int64_t *out)
{
    char text[40];
    size_t n;
    return json_string(doc, i, text, sizeof text, &n) && utc_parse(text, n, out);
}

/* ---- pieces -------------------------------------------------------------- */

static int read_sha(const json_doc *doc, int i, uint8_t out[SHA256_BYTES])
{
    char hex[SHA256_HEX + 1];
    size_t n;
    return json_string(doc, i, hex, sizeof hex, &n) && hex_decode(hex, n, out, SHA256_BYTES);
}

/* kind: 0 theme (no version), 1 release (opaque version), 2 launcher (X.Y.Z). */
static int read_blob(const json_doc *doc, int i, int kind, status_blob *blob)
{
    blob->present = 0;
    blob->version[0] = 0;
    if (json_kind(doc, i) != JSON_OBJECT) return 0;
    if (!read_sha(doc, json_get(doc, i, "sha256"), blob->sha256) || !json_u64(doc, json_get(doc, i, "size"), &blob->size))
        return 0;
    if (kind) {
        version v;
        if (!json_string(doc, json_get(doc, i, "version"), blob->version, sizeof blob->version, 0) ||
            blob->version[0] == 0)
            return 0;
        if (kind == 2 && !version_parse(blob->version, &v)) return 0;
    }
    blob->present = 1;
    return 1;
}

static int pick_title(const json_doc *doc, int titles, const char *lang, char *out, size_t cap)
{
    int value = -1;
    if (json_kind(doc, titles) == JSON_STRING) return json_string(doc, titles, out, cap, 0);
    if (json_kind(doc, titles) != JSON_OBJECT) return 0;
    if (lang) value = json_get(doc, titles, lang);
    if (json_kind(doc, value) != JSON_STRING) value = json_get(doc, titles, "en");
    if (json_kind(doc, value) != JSON_STRING) value = json_next(doc, json_first(doc, titles));
    return json_string(doc, value, out, cap, 0);
}

static int read_news(const json_doc *doc, int item, const char *lang, status_news *news)
{
    int image = json_get(doc, item, "image"), link = json_get(doc, item, "link");
    if (json_kind(doc, item) != JSON_OBJECT) return 0;
    news->has_image = 0;
    news->link[0] = 0;
    if (image >= 0 && !json_is_null(doc, image)) {
        if (!read_sha(doc, image, news->image)) return 0;
        news->has_image = 1;
    }
    if (!pick_title(doc, json_get(doc, item, "title"), lang, news->title, sizeof news->title)) return 0;
    if (link >= 0 && (!json_string(doc, link, news->link, sizeof news->link, 0) ||
                      !config_https_url_is_valid(news->link)))
        news->link[0] = 0; /* a bad link only disables the click, not the whole status */
    return 1;
}

/* ---- document ------------------------------------------------------------ */

status_error status_parse(launcher_status *st, const char *text, size_t len, json_token *tokens, uint32_t cap,
                          const char *lang)
{
    json_doc doc;
    uint64_t schema;
    int opens_at, news, launcher, item;

    st->news_count = 0;
    st->has_opens_at = 0;
    st->theme.present = st->release.present = st->launcher.present = 0;

    if (json_parse(&doc, text, len, tokens, cap) != JSON_OK || json_kind(&doc, 0) != JSON_OBJECT)
        return STATUS_ERR_JSON;
    if (!json_u64(&doc, json_get(&doc, 0, "schema"), &schema) || schema != STATUS_SCHEMA) return STATUS_ERR_SCHEMA;
    st->schema = (uint32_t)schema;
    if (!read_time(&doc, json_get(&doc, 0, "server_time"), &st->server_time)) return STATUS_ERR_TIME;
    opens_at = json_get(&doc, 0, "opens_at");
    if (opens_at >= 0 && !json_is_null(&doc, opens_at)) {
        if (!read_time(&doc, opens_at, &st->opens_at)) return STATUS_ERR_TIME;
        st->has_opens_at = 1;
    }
    if (!json_bool(&doc, json_get(&doc, 0, "open"), &st->open)) return STATUS_ERR_OPEN;

    news = json_get(&doc, 0, "news");
    if (news >= 0 && !json_is_null(&doc, news)) {
        if (json_kind(&doc, news) != JSON_ARRAY) return STATUS_ERR_NEWS;
        for (item = json_first(&doc, news); item >= 0 && st->news_count < STATUS_NEWS_MAX;
             item = json_next(&doc, item)) {
            if (!read_news(&doc, item, lang, &st->news[st->news_count])) return STATUS_ERR_NEWS;
            st->news_count++;
        }
    }

    if (!read_blob(&doc, json_get(&doc, 0, "theme"), 0, &st->theme)) return STATUS_ERR_BLOB;
    if (!read_blob(&doc, json_get(&doc, 0, "release"), 1, &st->release)) return STATUS_ERR_BLOB;
    launcher = json_get(&doc, 0, "launcher");
    if (launcher >= 0 && !json_is_null(&doc, launcher) && !read_blob(&doc, launcher, 2, &st->launcher))
        return STATUS_ERR_BLOB;
    return STATUS_OK;
}

const char *status_error_text(status_error error)
{
    switch (error) {
    case STATUS_OK: return "ok";
    case STATUS_ERR_JSON: return "status is not a valid JSON object";
    case STATUS_ERR_SCHEMA: return "unsupported status schema";
    case STATUS_ERR_TIME: return "invalid server_time or opens_at";
    case STATUS_ERR_OPEN: return "invalid or missing \"open\"";
    case STATUS_ERR_NEWS: return "invalid news item";
    case STATUS_ERR_BLOB: return "invalid theme, release or launcher blob";
    }
    return "unknown error";
}

int64_t status_seconds_to_open(const launcher_status *st)
{
    int64_t left;
    if (!st->has_opens_at) return -1;
    left = st->opens_at - st->server_time;
    return left > 0 ? left : 0;
}
