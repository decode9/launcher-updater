#!/usr/bin/env bash
# Creates a self-signed certificate for the dev server and prints the launcher pin:
# the SHA-256 of the certificate in DER form, as 64 lowercase hex digits.
# Usage: make-cert.sh [output-dir] [common-name]
set -euo pipefail
dir="${1:-.}"
name="${2:-localhost}"
mkdir -p "$dir"
openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:prime256v1 -nodes -days 825 \
    -subj "/CN=$name" -keyout "$dir/key.pem" -out "$dir/cert.pem" 2>/dev/null
openssl x509 -in "$dir/cert.pem" -outform DER | openssl dgst -sha256 -r | cut -d' ' -f1 | tee "$dir/fingerprint.txt"
