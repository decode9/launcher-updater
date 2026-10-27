#!/usr/bin/env python3
"""Development feed server for the launcher (Python 3 standard library only).

Implements the two routes of docs/spec.md over TLS:
  GET /v1/status          status document built from the example root
  GET /v1/blob/<sha256>   any file under the root, addressed by its SHA-256, with Range

The root folder holds:
  status.json    news and opening date (server_time, open, theme and release are filled in)
  theme.json     served as the "theme" blob
  release.json   served as the "release" blob (its "version" is copied into the status)
  status.json may also hold "launcher": {"version": "X.Y.Z", "file": "<path under the root>"}
                 to offer a self-update (the file's sha256 and size are filled in)
  anything else  also served as blobs (for example files/ referenced by release.json)

Usage: python3 server.py --root example-root --cert cert.pem --key key.pem --port 5103
"""

import argparse
import datetime
import hashlib
import json
import re
import ssl
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

BLOB_PATH = re.compile(r"^/v1/blob/([0-9a-f]{64})$")
RANGE = re.compile(r"^bytes=(\d+)-(\d*)$")
CHUNK = 1 << 16


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def utc(moment: datetime.datetime) -> str:
    return moment.astimezone(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def parse_utc(text: str) -> datetime.datetime:
    return datetime.datetime.strptime(text, "%Y-%m-%dT%H:%M:%SZ").replace(tzinfo=datetime.timezone.utc)


class Feed:
    def __init__(self, root: Path, force_open: bool):
        self.root = root.resolve()
        self.force_open = force_open
        self.blobs = {}
        self.scan()
        print(f"serving {len(self.blobs)} blobs from {self.root}")

    def scan(self):
        for path in sorted(self.root.rglob("*")):
            if path.is_file():
                self.blobs[sha256_file(path)] = path

    def find(self, sha: str):
        """Blob by hash; files changed since the start are found by scanning again."""
        if sha not in self.blobs:
            self.scan()
        path = self.blobs.get(sha)
        return path if path is not None and path.is_file() else None

    def blob_ref(self, name: str) -> dict:
        path = self.root / name
        return {"sha256": sha256_file(path), "size": path.stat().st_size}

    def status(self) -> dict:
        base = json.loads((self.root / "status.json").read_text(encoding="utf-8"))
        now = datetime.datetime.now(datetime.timezone.utc)
        opens_at = base.get("opens_at")
        is_open = self.force_open or (opens_at is not None and now >= parse_utc(opens_at))
        release = self.blob_ref("release.json")
        release["version"] = json.loads((self.root / "release.json").read_text(encoding="utf-8"))["version"]
        doc = {
            "schema": 1,
            "server_time": utc(now),
            "open": is_open,
            "opens_at": opens_at,
            "news": base.get("news", []),
            "theme": self.blob_ref("theme.json"),
            "release": release,
        }
        launcher = base.get("launcher")
        if launcher:
            doc["launcher"] = {"version": launcher["version"], **self.blob_ref(launcher["file"])}
        return doc


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    server_version = "launcher-dev-server"
    feed: Feed
    rate = 0  # bytes per second for blobs (0 = unlimited), to watch the progress bar

    def do_GET(self):  # noqa: N802 (http.server naming)
        if self.path == "/v1/status":
            body = json.dumps(self.feed.status(), ensure_ascii=False).encode("utf-8")
            self.respond(200, body, "application/json; charset=utf-8")
            return
        match = BLOB_PATH.match(self.path)
        path = self.feed.find(match.group(1)) if match else None
        if path is None:
            self.respond(404, b"not found\n", "text/plain")
            return
        self.send_blob(path)

    def send_blob(self, path: Path):
        size = path.stat().st_size
        first, last, status = 0, size - 1, 200
        header = self.headers.get("Range")
        if header is not None:
            match = RANGE.match(header.strip())
            if not match or int(match.group(1)) >= size:
                self.send_response(416)
                self.send_header("Content-Range", f"bytes */{size}")
                self.send_header("Content-Length", "0")
                self.send_header("Connection", "close")
                self.end_headers()
                return
            first = int(match.group(1))
            last = min(int(match.group(2)), size - 1) if match.group(2) else size - 1
            if last < first:
                self.respond(416, b"", "text/plain")
                return
            status = 206
        self.send_response(status)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Content-Length", str(last - first + 1))
        if status == 206:
            self.send_header("Content-Range", f"bytes {first}-{last}/{size}")
        self.send_header("Connection", "close")
        self.end_headers()
        with path.open("rb") as f:
            f.seek(first)
            left = last - first + 1
            while left:
                block = f.read(min(CHUNK, left))
                if not block:
                    break
                self.wfile.write(block)
                left -= len(block)
                if self.rate:
                    time.sleep(len(block) / self.rate)

    def respond(self, status: int, body: bytes, content_type: str):
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(body)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", type=Path, default=Path(__file__).parent / "example-root")
    parser.add_argument("--cert", type=Path, required=True)
    parser.add_argument("--key", type=Path, required=True)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=5103)
    parser.add_argument("--open", action="store_true", help="report open=true regardless of opens_at")
    parser.add_argument("--rate", type=int, default=0, help="limit blob downloads to this many KiB/s per request")
    args = parser.parse_args()
    Handler.rate = args.rate * 1024

    Handler.feed = Feed(args.root, args.open)
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.minimum_version = ssl.TLSVersion.TLSv1_2
    context.load_cert_chain(args.cert, args.key)
    server = ThreadingHTTPServer((args.host, args.port), Handler)
    server.socket = context.wrap_socket(server.socket, server_side=True)
    print(f"listening on https://{args.host}:{args.port}")
    server.serve_forever()


if __name__ == "__main__":
    main()
