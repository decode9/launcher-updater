/*
 * theme.h - The theme model: everything the launcher window looks like, from theme.json
 * (docs/theming.md, docs/theme.schema.json).
 *
 * The executable has no built-in design: a theme describes the window, colour tokens, fonts,
 * translated strings and a list of positioned elements. theme_parse turns the JSON into a
 * self-contained model (all text is copied into the model's own pool, so the JSON text and the
 * token array can be dropped afterwards). No allocation: the caller owns the theme_model (~47
 * KiB, keep it static) and the JSON token array.
 *
 * Open for extension, tolerant on input:
 *   - unknown top-level keys, unknown element fields and unknown style fields are ignored;
 *   - an element of an unknown type (or with an unusable rect / action) is skipped, the rest is
 *     drawn;
 *   - an invalid colour falls back to the field's default, an unknown font to the default font;
 *   - anything beyond the fixed limits below is dropped and flagged in `warnings`.
 * Only malformed JSON, a non-object root, an unsupported schema_version or a malformed
 * min_launcher_version fail the parse.
 *
 * Element types are parsed through a table (theme_elements.c): a new type is one table entry
 * plus its parse function.
 */
#ifndef LAUNCHER_CORE_THEME_H
#define LAUNCHER_CORE_THEME_H

#include <stddef.h>
#include <stdint.h>

#include "json.h"
#include "sha256.h"
#include "version.h"

#define THEME_SCHEMA_VERSION 1

/* Fixed limits. Extra entries are ignored (with a warning flag), never an error. */
#define THEME_MAX_ELEMENTS 64
#define THEME_MAX_TOKENS 32
#define THEME_MAX_FONTS 8
#define THEME_MAX_LANGS 8
#define THEME_MAX_STRINGS 128 /* per language */
#define THEME_MAX_BLOBS 32    /* distinct images + font files */
#define THEME_NAME_BYTES 32   /* token, font and language names, including the NUL */
#define THEME_POOL_BYTES 32768 /* all decoded text: font families, strings, keys, URLs */
#define THEME_MAX_SIDE 4096   /* window width/height bound */

/* Suggested size of the JSON token array for theme_parse (a full theme uses ~1-2k). */
#define THEME_JSON_TOKENS 4096

/* Colours are 0xRRGGBBAA. */
typedef uint32_t theme_color;
#define THEME_COLOR_NONE 0x00000000u  /* fully transparent: "do not draw" */
#define THEME_COLOR_TEXT 0xF0F0F0FFu  /* default text colour without a "text" token */
#define THEME_COLOR_BG 0x202428FFu    /* default window colour without a "bg" token */

/* A reference to a NUL-terminated string in the model's pool; 0 is the empty string. */
typedef uint16_t theme_sref;

typedef enum {
    THEME_OK = 0,
    THEME_ERR_JSON,    /* malformed JSON or too many JSON tokens */
    THEME_ERR_ROOT,    /* the root is not an object */
    THEME_ERR_SCHEMA,  /* schema_version missing, not an integer, 0 or newer than supported */
    THEME_ERR_VERSION  /* min_launcher_version present but not X.Y.Z */
} theme_error;

/* Warning flags (theme_model.warnings): what was ignored while parsing. */
enum {
    THEME_WARN_LIMIT_ELEMENTS = 1u << 0,
    THEME_WARN_LIMIT_TOKENS = 1u << 1,
    THEME_WARN_LIMIT_FONTS = 1u << 2,
    THEME_WARN_LIMIT_LANGS = 1u << 3,
    THEME_WARN_LIMIT_STRINGS = 1u << 4,
    THEME_WARN_LIMIT_BLOBS = 1u << 5,
    THEME_WARN_POOL_FULL = 1u << 6,
    THEME_WARN_UNKNOWN_TYPE = 1u << 7,  /* element of an unknown type skipped */
    THEME_WARN_BAD_ELEMENT = 1u << 8,   /* element skipped: no/invalid rect, action or url */
    THEME_WARN_BAD_COLOR = 1u << 9,     /* a colour fell back to its default */
    THEME_WARN_BAD_VALUE = 1u << 10,    /* another invalid value fell back to its default */
    THEME_WARN_CLIPPED = 1u << 11       /* an element was clipped to the window */
};

/* Launcher states, for visible_when. */
typedef enum {
    THEME_LS_CHECKING = 0,
    THEME_LS_LOCKED,
    THEME_LS_READY,
    THEME_LS_INSTALLING,
    THEME_LS_ERROR,
    THEME_LS_COUNT
} theme_launcher_state;
#define THEME_LS_ALL ((1u << THEME_LS_COUNT) - 1u)

/* Interaction states of an element's style. hover/pressed/disabled inherit from normal. */
typedef enum {
    THEME_ST_NORMAL = 0,
    THEME_ST_HOVER,
    THEME_ST_PRESSED,
    THEME_ST_DISABLED,
    THEME_ST_COUNT
} theme_state;

typedef enum {
    THEME_EL_IMAGE = 0,
    THEME_EL_TEXT,
    THEME_EL_NEWS,
    THEME_EL_PROGRESS,
    THEME_EL_COUNTDOWN,
    THEME_EL_STATUS,
    THEME_EL_BUTTON,
    THEME_EL_TYPE_COUNT
} theme_element_type;

typedef enum { THEME_ALIGN_LEFT = 0, THEME_ALIGN_CENTER, THEME_ALIGN_RIGHT } theme_align;

typedef enum {
    THEME_ACT_PLAY = 0,
    THEME_ACT_EXIT,
    THEME_ACT_MINIMIZE,
    THEME_ACT_OPEN_URL
} theme_action;

typedef struct {
    int32_t x, y, w, h;
} theme_rect;

typedef struct {
    uint8_t sha256[SHA256_BYTES];
    uint8_t is_font; /* 1: font file, 0: image */
} theme_blob;

typedef struct {
    char name[THEME_NAME_BYTES];
    theme_sref family;
    uint16_t size;   /* pixels, default 14 */
    uint16_t weight; /* 100..900, default 400 */
    int16_t file;    /* blob index of the font file, or -1 (system font by family) */
} theme_font;

/* A fully resolved style for one interaction state: no inheritance left to do. */
typedef struct {
    theme_color fill;    /* background; THEME_COLOR_NONE = none */
    theme_color color;   /* text */
    theme_color border;  /* THEME_COLOR_NONE = none */
    theme_color track;   /* progress: the empty part */
    uint16_t border_width;
    uint16_t radius;
    int16_t image;       /* blob index, or -1 */
    int16_t font;        /* font index, or -1 = built-in default */
} theme_style;

typedef struct {
    uint8_t type;          /* theme_element_type */
    uint8_t align;         /* theme_align */
    uint8_t visible_when;  /* bit per theme_launcher_state */
    uint8_t action;        /* button: theme_action */
    theme_rect rect;       /* already clipped to the window, never empty */
    theme_sref text;       /* string key (or literal); 0 = none */
    theme_sref url;        /* button open_url: https:// URL */
    int16_t image;         /* image element: blob index, or -1 */
    uint16_t interval;     /* news: seconds per item, default 8 */
    theme_style style[THEME_ST_COUNT];
} theme_element;

typedef struct {
    char name[THEME_NAME_BYTES];
    theme_color value;
} theme_token;

typedef struct {
    theme_sref key, value;
} theme_string;

typedef struct {
    char name[THEME_NAME_BYTES]; /* "en", "es", "pt-BR"... */
    theme_sref title;            /* window.title in this language, 0 = none */
    uint16_t string_count;
    theme_string strings[THEME_MAX_STRINGS];
} theme_lang;

typedef struct {
    uint32_t schema_version;
    version min_launcher;   /* 0.0.0 when absent */
    uint32_t warnings;      /* THEME_WARN_* */
    uint32_t skipped_elements;

    /* window */
    uint16_t width, height; /* default 960 x 600 */
    uint16_t radius;
    uint8_t draggable;      /* default 1 */
    theme_color background; /* window.background.color */
    int16_t background_image; /* blob index, or -1 */
    int16_t default_font;     /* the font named "body", or -1 */

    uint16_t token_count, font_count, lang_count, element_count, blob_count;
    theme_token tokens[THEME_MAX_TOKENS];
    theme_font fonts[THEME_MAX_FONTS];
    theme_lang langs[THEME_MAX_LANGS];
    theme_element elements[THEME_MAX_ELEMENTS];
    theme_blob blobs[THEME_MAX_BLOBS];

    uint32_t pool_used;
    char pool[THEME_POOL_BYTES];
} theme_model;

/* Parses theme.json into *theme (fully reset first). tokens/cap: scratch JSON token array,
 * only used during the call (THEME_JSON_TOKENS is a comfortable size). */
theme_error theme_parse(theme_model *theme, const char *text, size_t len, json_token *tokens,
                        uint32_t cap);

/* 1 if the theme needs a newer launcher than my_version ("X.Y.Z"); 0 otherwise (also when
 * my_version is malformed, so a broken build never loops on self-update). */
int theme_requires_update(const theme_model *theme, const char *my_version);

/* The NUL-terminated string behind a reference ("" for 0 or an invalid reference). */
const char *theme_str(const theme_model *theme, theme_sref ref);

/* Colour from a token name, "#RRGGBB" or "#RRGGBBAA" (hex digits in either case). Returns 1
 * and sets *out on success; returns 0 and sets *out = fallback otherwise. */
int theme_color_resolve(const theme_model *theme, const char *text, theme_color fallback,
                        theme_color *out);

/* Translated text: strings[lang][key], then strings[base language of lang][key] ("pt" for
 * "pt-BR"), then strings["en"][key], then key itself. lang may be 0. Never returns 0. */
const char *theme_text(const theme_model *theme, const char *lang, const char *key);

/* Window title in lang with the same fallback as theme_text, or "" if the theme has none. */
const char *theme_title(const theme_model *theme, const char *lang);

/* Elements in drawing order (first is drawn first, i.e. at the back). */
static inline uint32_t theme_element_count(const theme_model *theme) { return theme->element_count; }
const theme_element *theme_element_at(const theme_model *theme, uint32_t i); /* 0 if out of range */

/* Whether an element is shown in launcher state ls. */
int theme_element_visible(const theme_element *e, theme_launcher_state ls);

/* Resolved style of an element in an interaction state (out-of-range state = normal). */
const theme_style *theme_element_style(const theme_element *e, theme_state st);

/* Font by index (-1 or out of range: 0, meaning the built-in default font). */
const theme_font *theme_font_at(const theme_model *theme, int index);

/* Blobs (images, font files) the theme references, deduplicated, for the app to download and
 * cache before drawing. Copies up to cap SHA-256 digests into out (may be 0 to only count) and
 * returns the total number. */
uint32_t theme_required_blobs(const theme_model *theme, uint8_t (*out)[SHA256_BYTES],
                              uint32_t cap);

/* Variables for theme_format; a 0 member leaves "{name}" untouched in the output. */
typedef struct {
    const char *days, *hours, *minutes, *seconds;
    const char *percent, *version, *done, *total;
} theme_vars;

/* Expands {days} {hours} {minutes} {seconds} {percent} {version} {done} {total} in template
 * into out (always NUL-terminated when cap > 0). Unknown or unset variables and stray braces
 * are copied as they are. Returns 1, or 0 if the result was truncated. */
int theme_format(const char *template_text, const theme_vars *vars, char *out, size_t cap);

#endif
