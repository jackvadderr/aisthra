#!/bin/sh
set -eu

workspace=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
test -s "$workspace/src/aisthra.c"
test -s "$workspace/include/aisthra/aisthra.h"
test -s "$workspace/tests/support/aisthra_test_hooks.h"
test ! -d "$workspace/csrc"
test ! -d "$workspace/src/perception"
test ! -f "$workspace/examples/independent_consumer.mojo"
test ! -f "$workspace/benchmarks/aisthra_mojo_bridge_benchmark.c"
grep -Fq 'src/aisthra.c' "$workspace/CMakeLists.txt"
grep -Fq 'tests/abi/test_abi.c' "$workspace/CMakeLists.txt"
grep -Fq 'benchmarks/core/aisthra_benchmark.c' "$workspace/benchmarks/runners/run-benchmarks.sh"
if rg -n 'perception\.core|pixi-build-mojo|mojo-compiler' \
    "$workspace/README.md" "$workspace/CMakeLists.txt" "$workspace/benchmarks"; then
    echo 'aisthra-layout: active Mojo dependency detected' >&2
    exit 1
fi
printf '%s\n' 'aisthra-layout: PASS'
