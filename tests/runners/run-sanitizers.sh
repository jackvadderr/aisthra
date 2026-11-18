#!/usr/bin/env bash
set -euo pipefail
root="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
artifact_root="${AISTHRA_ARTIFACT_ROOT:-${root}/.artifacts}"
run_sanitizer() {
    local name="$1"
    local extra="$2"
    local dir="${artifact_root}/sanitizers/${name}"
    cmake -S "$root" -B "$dir" -DAISTHRA_BUILD_TESTS=ON -DAISTHRA_BUILD_EXAMPLES=OFF \
        -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_FLAGS="-fno-omit-frame-pointer $extra" \
        -DCMAKE_EXE_LINKER_FLAGS="$extra"
    cmake --build "$dir" --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-2}"
    # The installed shared-consumer test intentionally validates deployment;
    # under sanitizer builds its loader ordering is toolchain-dependent.
    ctest --test-dir "$dir" --exclude-regex 'aisthra\.installed-consumer' \
        --output-on-failure
}
run_sanitizer asan-ubsan '-fsanitize=address,undefined'
if [[ "${AISTHRA_RUN_TSAN:-0}" == 1 ]]; then
    run_sanitizer tsan '-fsanitize=thread'
else
    printf '%s\n' 'TSan: NOT_RUN (set AISTHRA_RUN_TSAN=1 to execute)'
fi
printf '%s\n' 'sanitizers: PASS'
