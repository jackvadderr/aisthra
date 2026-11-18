#!/usr/bin/env bash
set -euo pipefail

if [[ "${AISTHRA_ALLOW_FLASH:-0}" != 1 ]]; then
    printf '%s\n' 'ESP32_HARDWARE: NOT_TESTED (flashing requires AISTHRA_ALLOW_FLASH=1)' >&2
    exit 2
fi

if [[ -z "${IDF_PATH:-}" || ! -f "${IDF_PATH}/export.sh" ]]; then
    printf '%s\n' 'ESP32_HARDWARE: BLOCKED (set IDF_PATH to an ESP-IDF checkout)' >&2
    exit 2
fi

if [[ -z "${ESPPORT:-}" ]]; then
    printf '%s\n' 'ESP32_HARDWARE: BLOCKED (set ESPPORT after identifying the board)' >&2
    exit 2
fi

printf '%s\n' 'ESP32_HARDWARE: NOT_RUN (this repository runner never flashes implicitly)' >&2
exit 2
