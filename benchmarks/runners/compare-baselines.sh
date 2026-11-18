#!/usr/bin/env bash
set -euo pipefail
if [[ $# -ne 2 ]]; then
    printf 'usage: %s BASELINE.jsonl CURRENT.jsonl\n' "$0" >&2
    exit 2
fi
test -s "$1" -a -s "$2"
printf '%s\n' 'Matching scenario records:'
awk -F'[,:}]' '/"scenario"/ { print FILENAME ": " $0 }' "$1" "$2"
printf '%s\n' 'comparison: raw records displayed; workloads and units must match before interpretation'
