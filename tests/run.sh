#!/usr/bin/env bash
# Builds src/core with the host compiler (normal libc) and runs every tests/test_*.c.
# Usage: tests/run.sh            (CC=gcc tests/run.sh to use another compiler)
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/.." && pwd)"
out="$root/build/tests"
mkdir -p "$out"
cc="${CC:-clang}"

flags=(-std=c11 -g -O1 -Wall -Wextra -Werror -I"$root/src")
# Sanitizers catch out-of-bounds and UB in the parsers; skip them if the toolchain lacks them.
if echo 'int main(void){return 0;}' | "$cc" -x c - -fsanitize=address,undefined -o "$out/.probe" 2>/dev/null; then
    flags+=(-fsanitize=address,undefined -fno-sanitize-recover=all)
fi
rm -f "$out/.probe"

core=("$root"/src/core/*.c)
failed=0
for test in "$here"/test_*.c; do
    name="$(basename "$test" .c)"
    "$cc" "${flags[@]}" "$test" "${core[@]}" -o "$out/$name"
    if ! "$out/$name"; then failed=1; fi
done
# Theme examples against docs/theme.schema.json (schema part skipped without jsonschema).
if command -v python3 >/dev/null; then
    python3 "$here/test_theme_schema.py" || failed=1
else
    echo "python3 not found: test_theme_schema.py skipped"
fi
if [[ $failed -ne 0 ]]; then
    echo "FAILED" >&2
    exit 1
fi
echo "all tests passed"
