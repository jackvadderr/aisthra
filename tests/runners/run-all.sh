#!/usr/bin/env bash
set -euo pipefail
root="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
build_dir="${AISTHRA_BUILD_DIR:-${root}/.artifacts/build/all}"
cmake -S "$root" -B "$build_dir" -DAISTHRA_BUILD_TESTS=ON -DAISTHRA_BUILD_EXAMPLES=ON
cmake --build "$build_dir" --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-2}"
ctest --test-dir "$build_dir" --output-on-failure
printf '%s\n' 'Aisthra test suite: PASS'
