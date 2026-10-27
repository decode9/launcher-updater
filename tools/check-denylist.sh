#!/usr/bin/env bash
# Fails if any file in the repository mentions a denied term, case-insensitive, binaries
# included (build/launcher.exe, the file that gets signed, must stay clean too). Skipped: .git
# and build/tests (host test binaries, never shipped; sanitizer runtimes hold random bytes).
# tools/denylist.txt holds one base64-encoded term per line, so the list never matches itself.
# Usage: tools/check-denylist.sh [repo-root]
set -euo pipefail
root="${1:-$(cd "$(dirname "$0")/.." && pwd)}"
list="$root/tools/denylist.txt"
patterns=()
terms=0
while IFS= read -r line || [[ -n "$line" ]]; do
    [[ -z "$line" || "$line" == \#* ]] && continue
    patterns+=(-e "$(printf '%s' "$line" | base64 -d)")
    terms=$((terms + 1))
done < "$list"
[[ $terms -gt 0 ]] || { echo "denylist is empty" >&2; exit 2; }

# File names only: the matching text itself is never echoed.
if files="$(find "$root" \( -path "$root/.git" -o -path "$root/build/tests" \) -prune -o -type f -print0 |
           xargs -0 grep -laiF "${patterns[@]}" 2>/dev/null)"; then
    echo "denied term found in:" >&2
    sed "s|^$root/||" <<< "$files" >&2
    exit 1
fi
echo "denylist: clean ($terms terms)"
