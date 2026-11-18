#!/usr/bin/env bash
set -euo pipefail
root="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
artifact_root="${AISTHRA_ARTIFACT_ROOT:-${root}/.artifacts}"
build_dir="${artifact_root}/build/static-analysis"
if ! command -v scan-build >/dev/null 2>&1; then
    printf '%s\n' 'scan-build: NOT_AVAILABLE' >&2
    exit 2
fi
cmake -S "$root" -B "$build_dir" -DAISTHRA_BUILD_TESTS=ON -DAISTHRA_BUILD_EXAMPLES=OFF -DCMAKE_BUILD_TYPE=Debug
scan-build --status-bugs cmake --build "$build_dir" --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-2}"
printf '%s\n' 'scan-build: PASS'
