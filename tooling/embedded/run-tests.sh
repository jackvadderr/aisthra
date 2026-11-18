#!/usr/bin/env bash
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
target="${AISTHRA_ESP_TARGET:-${1:-esp32}}"
"${root}/tooling/embedded/run-qemu.sh" "$target"
printf '%s\n' "ESP_TESTS[$target]: PASS"
