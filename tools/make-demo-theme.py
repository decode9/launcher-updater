#!/usr/bin/env python3
"""Generates examples/theme-demo/ (Python 3 standard library only).

The demo theme exercises what the neutral theme does not: a background image, an image
element, per-state button images, several fonts and an open_url button. Every image is drawn
here from code (gradients and simple shapes), so the repository holds no third-party art.

Writes:
  examples/theme-demo/images/*.png
  examples/theme-demo/theme.json                (image fields hold the PNGs' SHA-256)
  examples/dev-server/example-root/theme.json   (same theme, served by the dev server)
  examples/dev-server/example-root/theme/*.png  (its images, served as blobs)

The PNG bytes depend on the local zlib, so regenerating may change the hashes; theme.json is
always rewritten to match the images written in the same run.

Usage: python3 tools/make-demo-theme.py
"""

import hashlib
import json
import struct
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEMO = ROOT / "examples" / "theme-demo"
DEV_ROOT = ROOT / "examples" / "dev-server" / "example-root"

WIDTH, HEIGHT = 960, 600


# ---------------------------------------------------------------- PNG writer


def png(width: int, height: int, pixel) -> bytes:
    """RGBA PNG; pixel(x, y) returns (r, g, b, a) with 0..255 ints."""
    rows = bytearray()
    for y in range(height):
        rows.append(0)  # filter: none
        for x in range(width):
            rows.extend(pixel(x, y))

    def chunk(kind: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))

    header = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) +
            chunk(b"IDAT", zlib.compress(bytes(rows), 9)) + chunk(b"IEND", b""))


def mix(a, b, t: float):
    t = max(0.0, min(1.0, t))
    return tuple(round(a[i] + (b[i] - a[i]) * t) for i in range(len(a)))


def rounded_alpha(x: int, y: int, w: int, h: int, r: int) -> int:
    """255 inside a rounded rectangle of size w x h, 0 outside (1 px soft edge)."""
    cx = min(max(x + 0.5, r), w - r)
    cy = min(max(y + 0.5, r), h - r)
    d = ((x + 0.5 - cx) ** 2 + (y + 0.5 - cy) ** 2) ** 0.5
    return round(255 * max(0.0, min(1.0, r - d + 0.5)))


# ---------------------------------------------------------------- images


def background() -> bytes:
    top, bottom, glow = (16, 24, 38), (8, 10, 16), (42, 70, 110)

    def pixel(x, y):
        t = y / (HEIGHT - 1)
        base = mix(top, bottom, t)
        band = max(0.0, 1.0 - abs(t - 0.32) / 0.22)  # soft horizon light
        return (*mix(base, glow, band * band * 0.6), 255)

    return png(WIDTH, HEIGHT, pixel)


def emblem() -> bytes:
    w, h = 280, 72
    gold_hi, gold_lo = (242, 214, 140), (176, 128, 56)

    def pixel(x, y):
        # A diamond on the left and a tapered bar: plain geometry, no lettering.
        dx, dy = abs(x - 36) / 30, abs(y - 36) / 30
        inner = abs(x - 36) / 14 + abs(y - 36) / 14
        if dx + dy <= 1.0 and inner > 1.0:
            return (*mix(gold_hi, gold_lo, y / h), 255)
        if inner <= 0.6:
            return (*gold_hi, 255)
        if 80 <= x < w - 8 and abs(y - 36) <= 3 - (x - 80) * 3 // (w - 88):
            return (*mix(gold_hi, gold_lo, (x - 80) / (w - 88)), 255)
        return (0, 0, 0, 0)

    return png(w, h, pixel)


def button(top, bottom, edge) -> bytes:
    w, h, r = 264, 64, 12

    def pixel(x, y):
        a = rounded_alpha(x, y, w, h, r)
        if a == 0:
            return (0, 0, 0, 0)
        inner = rounded_alpha(x - 2, y - 2, w - 4, h - 4, r - 2) if 2 <= x < w - 2 and 2 <= y < h - 2 else 0
        color = mix(top, bottom, y / (h - 1)) if inner else edge
        return (*color, a)

    return png(w, h, pixel)


IMAGES = {
    "background.png": background,
    "emblem.png": emblem,
    "play-normal.png": lambda: button((214, 160, 72), (150, 100, 36), (242, 214, 140)),
    "play-hover.png": lambda: button((236, 186, 96), (176, 124, 52), (255, 236, 180)),
    "play-pressed.png": lambda: button((150, 100, 36), (120, 78, 28), (200, 160, 90)),
}


# ---------------------------------------------------------------- theme


def theme(sha: dict) -> dict:
    return {
        "schema_version": 1,
        "min_launcher_version": "0.1.0",
        "window": {
            "width": WIDTH, "height": HEIGHT,
            "background": {"image": sha["background.png"], "color": "bg"},
            "radius": 14, "draggable": True,
            "title": {"en": "Example Game", "es": "Example Game"},
        },
        "tokens": {
            "bg": "#0A0D14FF", "glass": "#0A0D14B3", "glass_line": "#F2D68C33",
            "gold": "#E8C36AFF", "text": "#F3EEE2FF", "muted": "#A8A294FF",
            "hover": "#FFFFFF1A", "danger": "#D9483BFF", "disabled": "#3A3A40FF",
        },
        "fonts": {
            "body": {"family": "Segoe UI", "size": 14, "weight": 400},
            "label": {"family": "Segoe UI", "size": 12, "weight": 600},
            "title": {"family": "Georgia", "size": 30, "weight": 700},
            "button": {"family": "Georgia", "size": 20, "weight": 700},
            "clock": {"family": "Consolas", "size": 24, "weight": 400},
        },
        "strings": {
            "en": {
                "title": "Example Game", "news": "LATEST NEWS", "play": "PLAY", "website": "Website",
                "minimize": "–", "close": "×", "opens_in": "THE WORLD OPENS IN",
                "countdown": "{days}d {hours}:{minutes}:{seconds}",
                "progress": "{percent}%  ·  {done} / {total}", "version": "v{version}",
                "status.checking": "Checking for updates…", "status.locked": "Not open yet.",
                "status.ready": "Ready.", "status.installing": "Downloading update…",
                "status.error": "Could not reach the server.", "news.empty": "No news yet.",
            },
            "es": {
                "news": "ÚLTIMAS NOTICIAS", "play": "JUGAR", "website": "Sitio web",
                "opens_in": "EL MUNDO ABRE EN", "status.checking": "Buscando actualizaciones…",
                "status.locked": "Aún no abre.", "status.ready": "Listo.",
                "status.installing": "Descargando actualización…",
                "status.error": "No se pudo conectar con el servidor.", "news.empty": "Aún no hay noticias.",
            },
        },
        "elements": [
            {"type": "image", "rect": [40, 28, 280, 72], "image": sha["emblem.png"]},
            {"type": "text", "rect": [340, 40, 400, 44], "font": "title", "color": "gold", "text": "title"},
            {"type": "button", "rect": [872, 16, 32, 32], "action": "minimize", "text": "minimize",
             "font": "button", "color": "muted",
             "style": {"normal": {"radius": 16}, "hover": {"fill": "hover", "color": "text"}}},
            {"type": "button", "rect": [912, 16, 32, 32], "action": "exit", "text": "close",
             "font": "button", "color": "muted",
             "style": {"normal": {"radius": 16}, "hover": {"fill": "danger", "color": "text"}}},

            {"type": "text", "rect": [40, 128, 560, 320], "text": "",
             "style": {"normal": {"fill": "glass", "border": "glass_line", "border_width": 1, "radius": 12}}},
            {"type": "text", "rect": [64, 146, 512, 20], "font": "label", "color": "gold", "text": "news"},
            {"type": "news", "rect": [64, 176, 512, 252], "font": "body", "interval": 6},

            {"type": "text", "rect": [640, 128, 280, 120], "text": "", "visible_when": ["locked"],
             "style": {"normal": {"fill": "glass", "border": "glass_line", "border_width": 1, "radius": 12}}},
            {"type": "text", "rect": [640, 148, 280, 20], "font": "label", "color": "muted",
             "text": "opens_in", "align": "center", "visible_when": ["locked"]},
            {"type": "countdown", "rect": [640, 180, 280, 40], "font": "clock", "color": "gold",
             "text": "countdown", "align": "center", "visible_when": ["locked"]},

            {"type": "button", "rect": [640, 272, 280, 40], "action": "open_url",
             "url": "https://example.com/", "text": "website", "font": "body", "color": "text",
             "style": {"normal": {"border": "glass_line", "border_width": 1, "radius": 8},
                       "hover": {"fill": "hover", "border": "gold"}, "pressed": {"fill": "glass"}}},

            {"type": "status", "rect": [40, 480, 560, 22], "font": "body", "color": "muted",
             "visible_when": ["checking", "locked", "ready", "installing"]},
            {"type": "status", "rect": [40, 480, 560, 22], "font": "body", "color": "danger",
             "visible_when": ["error"]},
            {"type": "progress", "rect": [40, 512, 560, 10], "visible_when": ["installing"],
             "style": {"normal": {"fill": "gold", "track": "glass", "radius": 5,
                                  "border": "glass_line", "border_width": 1}}},
            {"type": "text", "rect": [40, 530, 560, 20], "font": "label", "color": "muted",
             "text": "progress", "visible_when": ["installing"]},
            {"type": "text", "rect": [840, 572, 104, 20], "font": "label", "color": "muted",
             "text": "version", "align": "right"},

            {"type": "button", "rect": [656, 496, 264, 64], "action": "play", "text": "play",
             "font": "button", "color": "bg",
             "style": {"normal": {"image": sha["play-normal.png"]},
                       "hover": {"image": sha["play-hover.png"]},
                       "pressed": {"image": sha["play-pressed.png"], "color": "text"},
                       "disabled": {"image": None, "fill": "disabled", "color": "muted", "radius": 12}}},
        ],
    }


def main() -> None:
    images = DEMO / "images"
    images.mkdir(parents=True, exist_ok=True)
    (DEV_ROOT / "theme").mkdir(parents=True, exist_ok=True)
    sha = {}
    for name, draw in IMAGES.items():
        data = draw()
        sha[name] = hashlib.sha256(data).hexdigest()
        (images / name).write_bytes(data)
        (DEV_ROOT / "theme" / name).write_bytes(data)
        print(f"{name:18} {len(data):7} bytes  {sha[name]}")
    text = json.dumps(theme(sha), indent=2, ensure_ascii=False) + "\n"
    (DEMO / "theme.json").write_text(text, encoding="utf-8")
    (DEV_ROOT / "theme.json").write_text(text, encoding="utf-8")
    print(f"theme.json written to {DEMO.relative_to(ROOT)} and {DEV_ROOT.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
