#!/usr/bin/env python3
"""Draws the launcher's generic application icon and writes it as a PNG-in-ICO file.

The design is our own and neutral (no game art): a rounded square with a vertical slate
gradient and a white "play" triangle. Sizes 16, 32, 48 and 256, each stored as a PNG image
inside the .ico (supported since Windows Vista). Pure standard library, deterministic output,
so the file is generated at build time and never committed.

Usage: tools/make-icon.py OUT.ico
"""
import struct
import sys
import zlib

SIZES = (16, 32, 48, 256)
SUPERSAMPLE = 4
TOP = (0x3A, 0x4A, 0x63)     # slate, lighter at the top
BOTTOM = (0x1E, 0x27, 0x36)  # darker at the bottom
MARK = (0xF4, 0xF6, 0xFA)    # play triangle


def _inside_rounded_square(x: float, y: float, radius: float) -> bool:
    """True if (x, y) in the unit square lies inside the square with rounded corners."""
    cx = min(max(x, radius), 1.0 - radius)
    cy = min(max(y, radius), 1.0 - radius)
    return (x - cx) ** 2 + (y - cy) ** 2 <= radius * radius


def _inside_triangle(x: float, y: float) -> bool:
    """Play triangle pointing right, optically centred in the unit square."""
    ax, ay, bx, by, cx, cy = 0.38, 0.27, 0.38, 0.73, 0.76, 0.50

    def side(px, py, qx, qy):
        return (x - qx) * (py - qy) - (px - qx) * (y - qy)

    d1, d2, d3 = side(ax, ay, bx, by), side(bx, by, cx, cy), side(cx, cy, ax, ay)
    has_neg = d1 < 0 or d2 < 0 or d3 < 0
    has_pos = d1 > 0 or d2 > 0 or d3 > 0
    return not (has_neg and has_pos)


def render(size: int) -> bytes:
    """Returns size x size RGBA pixels, anti-aliased by supersampling."""
    margin = 1.0 / 32.0  # small transparent border, like system icons
    radius = 0.2
    n = SUPERSAMPLE
    pixels = bytearray()
    for py in range(size):
        for px in range(size):
            bg = mark = 0
            for sy in range(n):
                for sx in range(n):
                    u = (px + (sx + 0.5) / n) / size
                    v = (py + (sy + 0.5) / n) / size
                    iu = (u - margin) / (1.0 - 2 * margin)
                    iv = (v - margin) / (1.0 - 2 * margin)
                    if 0.0 <= iu <= 1.0 and 0.0 <= iv <= 1.0 and _inside_rounded_square(iu, iv, radius):
                        bg += 1
                        if _inside_triangle(iu, iv):
                            mark += 1
            total = n * n
            if bg == 0:
                pixels += b"\0\0\0\0"
                continue
            t = (py + 0.5) / size
            base = [TOP[i] + (BOTTOM[i] - TOP[i]) * t for i in range(3)]
            w = mark / bg
            rgb = [round(base[i] * (1 - w) + MARK[i] * w) for i in range(3)]
            pixels += bytes(rgb) + bytes([round(255 * bg / total)])
    return bytes(pixels)


def png(size: int, rgba: bytes) -> bytes:
    def chunk(tag: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data))

    stride = size * 4
    raw = b"".join(b"\0" + rgba[y * stride:(y + 1) * stride] for y in range(size))
    header = struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header)
            + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def ico(images: list[tuple[int, bytes]]) -> bytes:
    out = struct.pack("<HHH", 0, 1, len(images))
    offset = 6 + 16 * len(images)
    body = b""
    for size, data in images:
        dim = 0 if size >= 256 else size  # 0 means 256 in the directory entry
        out += struct.pack("<BBBBHHII", dim, dim, 0, 0, 1, 32, len(data), offset + len(body))
        body += data
    return out + body


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__.strip().splitlines()[-1], file=sys.stderr)
        return 2
    images = [(size, png(size, render(size))) for size in SIZES]
    with open(sys.argv[1], "wb") as f:
        f.write(ico(images))
    return 0


if __name__ == "__main__":
    sys.exit(main())
