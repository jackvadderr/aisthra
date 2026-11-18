#!/usr/bin/env bash
set -euo pipefail
root="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
build_root="${AISTHRA_STRESS_BUILD_ROOT:-${TMPDIR:-/tmp}/aisthra-stress-build}"
binary="$build_root/aisthra-core-benchmark"
output="${AISTHRA_STRESS_OUTPUT:-${TMPDIR:-/tmp}/aisthra-stress.jsonl}"
mkdir -p "$build_root"
cc_bin="${CC:-cc}"
cmake -S "$root" -B "$build_root/library" -DAISTHRA_BUILD_TESTS=OFF -DAISTHRA_BUILD_EXAMPLES=OFF \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_FLAGS='-O2 -DNDEBUG'
cmake --build "$build_root/library" --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-2}"
"$cc_bin" -std=c17 -O2 -DNDEBUG -Wall -Wextra -Wpedantic -I"$root/include" \
    "$root/benchmarks/core/aisthra_benchmark.c" -L"$build_root/library" \
    -Wl,-rpath,"$build_root/library" -laisthra -lm -pthread -o "$binary"
stress_operations="${AISTHRA_STRESS_OPERATIONS:-512}"
stress_repetitions="${AISTHRA_STRESS_REPETITIONS:-5}"
stress_warmup="${AISTHRA_STRESS_WARMUP:-5}"
LD_LIBRARY_PATH="$build_root/library${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    "$binary" --repetitions "$stress_repetitions" --operations "$stress_operations" \
    --warmup "$stress_warmup" --output "$output"
printf 'stress benchmark: %s\n' "$output"
