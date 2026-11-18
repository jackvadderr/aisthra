#!/usr/bin/env bash
set -euo pipefail

workspace="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
build_root="${AISTHRA_BENCHMARK_BUILD_ROOT:-${TMPDIR:-/tmp}/aisthra-benchmark-build}"
binary="${build_root}/aisthra-core-benchmark"
output="${AISTHRA_BENCHMARK_OUTPUT:-${TMPDIR:-/tmp}/aisthra-core-benchmark.jsonl}"
cc_bin="${CC:-cc}"

cmake -S "${workspace}" -B "${build_root}/library" \
    -DCMAKE_BUILD_TYPE=Release \
    -DAISTHRA_BUILD_TESTS=OFF \
    -DAISTHRA_BUILD_EXAMPLES=OFF \
    -DCMAKE_C_FLAGS="-O2 -DNDEBUG"
cmake --build "${build_root}/library" -j"${CMAKE_BUILD_PARALLEL_LEVEL:-2}"

"${cc_bin}" -std=c17 -O2 -DNDEBUG -Wall -Wextra -Wpedantic \
    -I"${workspace}/include" \
    "${workspace}/benchmarks/core/aisthra_benchmark.c" \
    -L"${build_root}/library" -Wl,-rpath,"${build_root}/library" \
    -laisthra -lm -pthread -o "${binary}"

LD_LIBRARY_PATH="${build_root}/library${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}" \
    "${binary}" --output "${output}"
printf 'benchmark: %s\n' "${output}"
