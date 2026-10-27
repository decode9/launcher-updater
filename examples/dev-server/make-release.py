#!/usr/bin/env python3
"""Generates a larger example release for install tests (Python 3 standard library only).

Writes example-root/generated/ (ignored by git) and rewrites example-root/release.json with:
  file  data/big.pak    20 MiB, deterministic bytes, in 8 MiB chunks (each chunk is a blob file)
  patch data/big.pak    appends a 1 MiB tail and rewrites a few bytes inside the base
  file  game.pak        the small example file
  line  settings.ini    Server=play.example.com
and prints the SHA-256 every file must have after the install.

Usage: python3 make-release.py [--root example-root] [--version 2030.01.01.2]
"""

import argparse
import hashlib
import json
import random
from pathlib import Path

CHUNK = 8 << 20


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", type=Path, default=Path(__file__).parent / "example-root")
    parser.add_argument("--version", default="2030.01.01.2")
    args = parser.parse_args()

    out = args.root / "generated"
    out.mkdir(parents=True, exist_ok=True)
    rng = random.Random(20300101)
    # Bytes 0x80-0xFF only: no ASCII text can appear by chance (tools/check-denylist.sh scans
    # every file in the tree, generated ones included).
    high = bytes(i | 0x80 for i in range(256))
    base = rng.randbytes(20 << 20).translate(high)
    tail = rng.randbytes(1 << 20).translate(high)
    writes = [(100, bytes.fromhex("deadbeef")), (10_000_000, bytes.fromhex("0102"))]

    chunks = []
    for index, offset in enumerate(range(0, len(base), CHUNK)):
        part = base[offset:offset + CHUNK]
        (out / f"big-{index}.bin").write_bytes(part)
        chunks.append({"sha256": sha(part), "size": len(part)})
    (out / "big-tail.bin").write_bytes(tail)

    result = bytearray(base + tail)
    for offset, data in writes:
        result[offset:offset + len(data)] = data
    game = (args.root / "files" / "game.pak").read_bytes()

    manifest = {
        "schema": 1,
        "version": args.version,
        "ops": [
            {"op": "file", "path": "data/big.pak", "sha256": sha(base), "size": len(base), "chunks": chunks},
            {"op": "patch", "path": "data/big.pak", "base_sha256": sha(base), "base_size": len(base),
             "writes": [{"offset": o, "data": d.hex()} for o, d in writes],
             "append": {"sha256": sha(tail), "size": len(tail)},
             "result_sha256": sha(bytes(result)), "result_size": len(result)},
            {"op": "file", "path": "game.pak", "sha256": sha(game), "size": len(game),
             "chunks": [{"sha256": sha(game), "size": len(game)}]},
            {"op": "line", "path": "settings.ini", "prefix": "Server=", "line": "Server=play.example.com"},
        ],
    }
    (args.root / "release.json").write_text(json.dumps(manifest, indent=1) + "\n", encoding="utf-8")
    print(f"release {args.version}: {len(chunks)} chunks")
    print(f"expected data/big.pak {sha(bytes(result))} {len(result)}")
    print(f"expected game.pak     {sha(game)} {len(game)}")


if __name__ == "__main__":
    main()
