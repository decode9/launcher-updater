#!/usr/bin/env python3
"""Validates the example themes against docs/theme.schema.json and checks their blobs.

Needs the jsonschema package (pip install jsonschema); without it the schema part is skipped
with a notice, the blob checks still run (REQUIRE_JSONSCHEMA=1 makes it an error, as in CI).
Run by tests/run.sh and by CI.
"""

import hashlib
import json
import os
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SCHEMA = ROOT / "docs" / "theme.schema.json"
DEV_ROOT = ROOT / "examples" / "dev-server" / "example-root"


def themes():
    found = sorted(ROOT.glob("examples/theme-*/theme.json"))
    if (DEV_ROOT / "theme.json").exists():
        found.append(DEV_ROOT / "theme.json")
    return found


def referenced_blobs(node):
    """Every 64-hex string under an "image" or "file" key."""
    if isinstance(node, dict):
        for key, value in node.items():
            if key in ("image", "file") and isinstance(value, str):
                yield value
            else:
                yield from referenced_blobs(value)
    elif isinstance(node, list):
        for item in node:
            yield from referenced_blobs(item)


def main() -> int:
    failures = 0
    files = themes()
    if not files:
        print("test_theme_schema: no example themes found", file=sys.stderr)
        return 1

    try:
        import jsonschema
    except ImportError:
        if os.environ.get("REQUIRE_JSONSCHEMA"):
            print("test_theme_schema: jsonschema is required (REQUIRE_JSONSCHEMA)", file=sys.stderr)
            return 1
        jsonschema = None
        print("test_theme_schema: jsonschema not installed, schema validation skipped "
              "(pip install jsonschema)")

    if jsonschema is not None:
        schema = json.loads(SCHEMA.read_text(encoding="utf-8"))
        jsonschema.Draft202012Validator.check_schema(schema)
        validator = jsonschema.Draft202012Validator(schema)
        for path in files:
            errors = sorted(validator.iter_errors(json.loads(path.read_text(encoding="utf-8"))),
                            key=lambda e: list(e.absolute_path))
            for error in errors:
                failures += 1
                where = "/".join(str(p) for p in error.absolute_path)
                print(f"{path.relative_to(ROOT)}: {where}: {error.message}", file=sys.stderr)

    # Every image/font a theme references must exist next to it, addressed by content.
    for path in files:
        theme = json.loads(path.read_text(encoding="utf-8"))
        available = {hashlib.sha256(p.read_bytes()).hexdigest()
                     for p in path.parent.rglob("*") if p.is_file()}
        for sha in referenced_blobs(theme):
            if sha not in available:
                failures += 1
                print(f"{path.relative_to(ROOT)}: blob {sha} not found under "
                      f"{path.parent.relative_to(ROOT)}", file=sys.stderr)

    print(f"test_theme_schema.py  {len(files)} themes  {'FAIL' if failures else 'ok'}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
