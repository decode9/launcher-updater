#!/usr/bin/env python3
"""Generates examples/theme-showcase/ (Python 3 standard library only).

The showcase theme exercises what launcher 0.3.0 added to themes: a transparent window
(per-pixel alpha) with an emblem that sticks out of a gradient frame, a white news panel with
filter tabs (`filter` buttons, `selected` state) above a `news_list`, round side `open_url`
buttons, `options` and `language` menu buttons, a large image Play button whose hover, pressed
and disabled looks come from `brightness`/`opacity` instead of extra images, and a gradient
progress bar with the status line. Every image is drawn here from code (plain geometry, no
lettering), so the repository holds no third-party art.

Writes:
  examples/theme-showcase/images/*.png
  examples/theme-showcase/theme.json   (image fields hold the PNGs' SHA-256)

The PNG bytes depend on the local zlib, so regenerating may change the hashes; theme.json is
always rewritten to match the images written in the same run.

Usage: python3 tools/make-showcase-theme.py
Preview: wine build/launcher.exe --theme-preview examples/theme-showcase [--lang es]
"""

import hashlib
import importlib.util
import json
import math
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "examples" / "theme-showcase"

# The PNG writer and colour helpers are shared with the demo theme generator (loaded by path:
# its file name is not a module name). No .pyc is left in tools/.
sys.dont_write_bytecode = True
_spec = importlib.util.spec_from_file_location("make_demo_theme", Path(__file__).with_name("make-demo-theme.py"))
_demo = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_demo)
png, mix, rounded_alpha = _demo.png, _demo.mix, _demo.rounded_alpha

WIDTH, HEIGHT = 1000, 660


# ---------------------------------------------------------------- images


def coverage(d: float) -> float:
    """Antialiased coverage of a pixel whose centre is d pixels inside an edge (d < 0: outside)."""
    return max(0.0, min(1.0, d + 0.5))


def emblem() -> bytes:
    """A faceted gem in front of a gold ring: geometry only, transparent around it."""
    w, h = 220, 150
    cx, cy = w / 2, h / 2
    gold_hi, gold_lo, ink = (255, 226, 140), (196, 132, 40), (24, 18, 36)
    gem_hi, gem_lo = (190, 150, 255), (88, 40, 160)

    def pixel(x, y):
        px, py = x + 0.5 - cx, y + 0.5 - cy
        # Gem: a diamond |x|/56 + |y|/64 <= 1 with a 4 px dark outline.
        k = abs(px) / 56 + abs(py) / 64
        gem_d = (1 - k) * 42  # roughly pixels inside the diamond edge
        if gem_d > -0.5:
            a = coverage(gem_d)
            if gem_d < 4:
                return (*ink, round(255 * a))
            facet = 0.35 if (px < 0) == (py < 0) else 0.0
            return (*mix(gem_hi, gem_lo, (py + 64) / 128 + facet), 255)
        # Ring: an elliptic band behind the gem, with a dark rim.
        r = math.hypot(px / 1.45, py)
        band = 9 - abs(r - 60)
        if band > -0.5:
            a = coverage(band)
            color = ink if band < 2.5 else mix(gold_hi, gold_lo, (py + 64) / 128)
            return (*color, round(255 * a))
        return (0, 0, 0, 0)

    return png(w, h, pixel)


def play_button() -> bytes:
    """A wide pill with a warm gradient, a dark rim and a soft top highlight."""
    w, h, r = 260, 104, 52
    top, bottom, rim, shine = (255, 196, 72), (226, 96, 40), (40, 20, 30), (255, 240, 200)

    def pixel(x, y):
        a = rounded_alpha(x, y, w, h, r)
        if a == 0:
            return (0, 0, 0, 0)
        inner = rounded_alpha(x - 4, y - 4, w - 8, h - 8, r - 4) if 4 <= x < w - 4 and 4 <= y < h - 4 else 0
        if not inner:
            return (*rim, a)
        color = mix(top, bottom, y / (h - 1))
        if y < h * 0.42:
            color = mix(color, shine, 0.35 * (1 - y / (h * 0.42)))
        return (*mix(rim, color, inner / 255), a)

    return png(w, h, pixel)


def round_icon(glyph) -> bytes:
    """A 56 px dark disc with a light rim and a simple glyph; glyph(px, py) -> 0..1 coverage,
    with px, py relative to the centre."""
    s = 56
    c = s / 2
    disc, disc_lo, rim, light = (52, 46, 84), (26, 22, 44), (255, 226, 140), (250, 248, 255)

    def pixel(x, y):
        px, py = x + 0.5 - c, y + 0.5 - c
        d = c - math.hypot(px, py)
        if d <= -0.5:
            return (0, 0, 0, 0)
        a = round(255 * coverage(d))
        if d < 3:
            return (*rim, a)
        g = glyph(px, py)
        return (*mix(mix(disc, disc_lo, y / s), light, g), a)

    return png(s, s, pixel)


def glyph_globe(px, py):
    r = math.hypot(px, py)
    ring = coverage(1.6 - abs(r - 13))
    meridian = coverage(1.6 - abs(math.hypot(px * 2.2, py) - 13)) if r < 13.5 else 0.0
    equator = coverage(1.6 - abs(py)) if r < 13 else 0.0
    return max(ring, meridian, equator)


def glyph_bubble(px, py):
    body = coverage(1 - (abs(px) / 15) ** 2.5 - (abs(py + 3) / 10) ** 2.5) if abs(px) < 16 and abs(py + 3) < 11 else 0.0
    tail = 1.0 if 0 <= py - 6 <= 8 and -10 <= px <= -10 + (14 - (py - 6) * 1.6) else 0.0
    return max(min(1.0, body * 4), tail)


def glyph_bag(px, py):
    box = 1.0 if abs(px) <= 12 and -4 <= py <= 13 else 0.0
    handle = coverage(1.8 - abs(math.hypot(px, py + 4) - 7)) if py <= -4 else 0.0
    return max(box, handle)


IMAGES = {
    "emblem.png": emblem,
    "play.png": play_button,
    "icon-website.png": lambda: round_icon(glyph_globe),
    "icon-community.png": lambda: round_icon(glyph_bubble),
    "icon-store.png": lambda: round_icon(glyph_bag),
}


# ---------------------------------------------------------------- theme

EN = {
    "language.name": "English",
    "title": "Showcase",
    "tab.all": "All", "tab.events": "Events", "tab.updates": "Updates", "tab.info": "Info",
    "more": "+",
    "news.category.events": "Event", "news.category.updates": "Update", "news.category.info": "Info",
    "news.date": "{mm}-{dd}",
    "news.empty": "No news in this category.",
    "options": "Options", "language": "Language", "play": "PLAY",
    "minimize": "–", "close": "×",
    "opens_in": "Opens in", "countdown": "{days}d {hours}:{minutes}:{seconds}",
    "progress": "{percent}%  ·  {done} / {total}", "version": "v{version}",
    "menu.repair": "Verify and repair files", "menu.open_folder": "Open game folder",
    "menu.game_settings": "Game settings",
    "status.checking": "Checking for updates…", "status.locked": "The server is not open yet.",
    "status.ready": "Ready to play.", "status.installing": "Downloading…",
    "status.error": "Could not reach the server.",
    "status.no_release": "No version has been published yet.",
    "status.repairing": "Checking and repairing the game files…",
    "status.repair_failed": "The repair failed. Try again from Options.",
    "status.settings_failed": "The game settings could not be started.",
}

ES = {
    "language.name": "Español",
    "title": "Showcase",
    "tab.all": "Todo", "tab.events": "Eventos", "tab.updates": "Parches", "tab.info": "Info",
    "news.category.events": "Evento", "news.category.updates": "Parche", "news.category.info": "Info",
    "news.date": "{dd}/{mm}",
    "news.empty": "No hay noticias en esta categoría.",
    "options": "Opciones", "language": "Idioma", "play": "JUGAR",
    "opens_in": "Abre en",
    "menu.repair": "Verificar y reparar archivos", "menu.open_folder": "Abrir carpeta del juego",
    "menu.game_settings": "Configuración del juego",
    "status.checking": "Buscando actualizaciones…", "status.locked": "El servidor aún no ha abierto.",
    "status.ready": "Listo para jugar.", "status.installing": "Descargando…",
    "status.error": "No se pudo conectar con el servidor.",
    "status.no_release": "Todavía no se ha publicado ninguna versión.",
    "status.repairing": "Verificando y reparando los archivos del juego…",
    "status.repair_failed": "La reparación falló. Inténtalo de nuevo desde Opciones.",
    "status.settings_failed": "No se pudo abrir la configuración del juego.",
}


def tab(x: int, category: str, key: str) -> dict:
    return {"type": "button", "rect": [x, 170, 96, 32], "action": "filter", "category": category,
            "text": key, "font": "tab", "color": "ink_soft",
            "style": {"normal": {"fill": "chip", "radius": 16},
                      "hover": {"fill": "chip_hover"},
                      "pressed": {"fill": "chip_hover", "color": "ink"},
                      "selected": {"fill": "accent", "color": "white"}}}


def side_button(y: int, url: str, image: str) -> dict:
    return {"type": "button", "rect": [938, y, 56, 56], "action": "open_url", "url": url, "text": "",
            "style": {"normal": {"image": image},
                      "hover": {"brightness": 25},
                      "pressed": {"brightness": -25}}}


def menu_button(y: int, action: str, key: str) -> dict:
    return {"type": "button", "rect": [700, y, 220, 46], "action": action, "text": key,
            "font": "button", "color": "white",
            "style": {"normal": {"fill": "glass", "border": "white_line", "border_width": 2, "radius": 23},
                      "hover": {"fill": "glass_hover", "border": "white"},
                      "pressed": {"fill": "ink"}}}


def theme(sha: dict) -> dict:
    return {
        "schema_version": 1,
        "min_launcher_version": "0.3.0",
        "window": {
            "width": WIDTH, "height": HEIGHT, "transparent": True,
            "background": {"image": None, "color": "none"},
            "radius": 0, "draggable": True,
            "title": {"en": "Showcase", "es": "Showcase"},
        },
        "tokens": {
            "none": "#00000000", "bg": "#00000000", "text": "#1E2230FF",
            "ink": "#14121EFF", "ink_soft": "#4A4F5CFF", "white": "#FFFFFFFF",
            "white_line": "#FFFFFFB3", "paper": "#FFFFFFFF",
            "chip": "#EEF0F5FF", "chip_hover": "#E0E4EDFF",
            "accent": "#5B3FD9FF", "accent_soft": "#EFEAFFFF",
            "frame_a": "#FFC94AFF", "frame_b": "#E0603AFF", "frame_c": "#6A2FB8FF",
            "glass": "#14121E99", "glass_hover": "#14121ECC",
            "bar_a": "#3FC1F5FF", "bar_b": "#5B3FD9FF", "track": "#FFFFFF33",
            "danger": "#FF6B6BFF", "muted": "#D8D2EEFF",
        },
        "fonts": {
            "body": {"family": "Segoe UI", "size": 14, "weight": 400},
            "row": {"family": "Segoe UI", "size": 14, "weight": 600},
            "tag": {"family": "Segoe UI", "size": 11, "weight": 700},
            "tab": {"family": "Segoe UI", "size": 13, "weight": 600},
            "button": {"family": "Segoe UI", "size": 16, "weight": 600},
            "play": {"family": "Segoe UI", "size": 30, "weight": 800},
            "clock": {"family": "Consolas", "size": 22, "weight": 400},
        },
        "strings": {"en": EN, "es": ES},
        "elements": [
            # The frame: a gradient box with a black border; the window is transparent around it.
            {"type": "text", "rect": [24, 70, 940, 566], "text": "",
             "style": {"normal": {"gradient": {"colors": ["frame_a", "frame_b", "frame_c"], "angle": 60},
                                  "border": "ink", "border_width": 3, "radius": 28}}},
            # The emblem sticks out of the frame's top edge.
            {"type": "image", "rect": [390, 0, 220, 150], "image": sha["emblem.png"]},
            {"type": "button", "rect": [860, 88, 34, 34], "action": "minimize", "text": "minimize",
             "font": "button", "color": "white",
             "style": {"normal": {"radius": 17}, "hover": {"fill": "glass"}, "pressed": {"fill": "glass_hover"}}},
            {"type": "button", "rect": [900, 88, 34, 34], "action": "exit", "text": "close",
             "font": "button", "color": "white",
             "style": {"normal": {"radius": 17}, "hover": {"fill": "danger"}, "pressed": {"fill": "ink"}}},

            # White news panel: filter tabs, "+" link, and the list.
            {"type": "text", "rect": [60, 150, 600, 352], "text": "",
             "style": {"normal": {"fill": "paper", "border": "ink", "border_width": 2, "radius": 18}}},
            tab(80, "", "tab.all"),
            tab(184, "events", "tab.events"),
            tab(288, "updates", "tab.updates"),
            tab(392, "info", "tab.info"),
            {"type": "button", "rect": [608, 170, 32, 32], "action": "open_url",
             "url": "https://example.com/news", "text": "more", "font": "button", "color": "white",
             "style": {"normal": {"fill": "accent", "radius": 16}, "hover": {"fill": "frame_c"},
                       "pressed": {"fill": "ink"}}},
            {"type": "news_list", "rect": [72, 216, 576, 280], "font": "row", "color": "text",
             "row_height": 40, "tag_width": 76, "date_width": 52, "gap": 12, "date_format": "news.date",
             "tag_style": {"fill": "accent", "color": "white", "font": "tag", "radius": 8},
             "style": {"normal": {"radius": 10},
                       "hover": {"fill": "accent_soft", "color": "accent"},
                       "pressed": {"fill": "chip_hover", "color": "ink"}}},

            # Round side buttons hanging off the frame's right edge.
            side_button(176, "https://example.com/", sha["icon-website.png"]),
            side_button(244, "https://example.com/community", sha["icon-community.png"]),
            side_button(312, "https://example.com/store", sha["icon-store.png"]),

            # Right column: menus and the countdown.
            menu_button(150, "options", "options"),
            menu_button(208, "language", "language"),
            {"type": "text", "rect": [700, 290, 220, 20], "font": "tab", "color": "white",
             "text": "opens_in", "align": "center", "visible_when": "locked"},
            {"type": "countdown", "rect": [700, 312, 220, 36], "font": "clock", "color": "white",
             "text": "countdown", "align": "center", "visible_when": "locked"},
            {"type": "text", "rect": [700, 400, 220, 20], "font": "tab", "color": "muted",
             "text": "version", "align": "center"},

            # Bottom panel: status line and a gradient progress bar.
            {"type": "text", "rect": [60, 520, 600, 92], "text": "",
             "style": {"normal": {"fill": "glass", "radius": 18}}},
            {"type": "status", "rect": [84, 532, 552, 24], "color": "white",
             "visible_when": ["checking", "locked", "ready", "installing"]},
            {"type": "status", "rect": [84, 532, 552, 24], "color": "danger", "visible_when": "error"},
            {"type": "progress", "rect": [84, 564, 552, 14], "text": "", "visible_when": "installing",
             "style": {"normal": {"track": "track", "radius": 7,
                                  "gradient": {"colors": ["bar_a", "bar_b"], "angle": 0}}}},
            {"type": "text", "rect": [84, 582, 552, 20], "font": "tab", "color": "muted",
             "text": "progress", "visible_when": "installing"},

            # Play: one image; hover, pressed and disabled are brightness/opacity changes.
            {"type": "button", "rect": [680, 528, 260, 104], "action": "play", "text": "play",
             "font": "play", "color": "ink",
             "style": {"normal": {"image": sha["play.png"]},
                       "hover": {"brightness": 15},
                       "pressed": {"brightness": -20},
                       "disabled": {"brightness": -30, "opacity": 55, "color": "glass_hover"}}},
        ],
    }


def main() -> None:
    images = OUT / "images"
    images.mkdir(parents=True, exist_ok=True)
    sha = {}
    for name, draw in IMAGES.items():
        data = draw()
        sha[name] = hashlib.sha256(data).hexdigest()
        (images / name).write_bytes(data)
        print(f"{name:20} {len(data):7} bytes  {sha[name]}")
    text = json.dumps(theme(sha), indent=2, ensure_ascii=False) + "\n"
    (OUT / "theme.json").write_text(text, encoding="utf-8")
    print(f"theme.json written to {OUT.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
