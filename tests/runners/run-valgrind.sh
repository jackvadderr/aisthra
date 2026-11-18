#!/usr/bin/env bash
set -euo pipefail
root="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
if ! command -v valgrind >/dev/null 2>&1; then
    printf '%s\n' 'valgrind: NOT_AVAILABLE' >&2
    exit 2
fi
artifact_root="${AISTHRA_ARTIFACT_ROOT:-${root}/.artifacts}"
build_dir="${artifact_root}/build/valgrind"
log_dir="${artifact_root}/valgrind"
mkdir -p "$log_dir"
cmake -S "$root" -B "$build_dir" -DAISTHRA_BUILD_TESTS=ON -DAISTHRA_BUILD_EXAMPLES=OFF -DCMAKE_BUILD_TYPE=Debug
cmake --build "$build_dir" --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-2}"
for test_bin in aisthra_test_core aisthra_test_advanced aisthra_test_hardening; do
    valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all --track-origins=yes \
        --error-exitcode=99 --log-file="$log_dir/${test_bin}.log" "$build_dir/$test_bin"
done
printf '%s\n' 'valgrind memcheck: PASS'
