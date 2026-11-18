#!/usr/bin/env bash
set -euo pipefail

workspace="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
artifact_root="${AISTHRA_ARTIFACT_ROOT:-${workspace}/.artifacts/valgrind-tools}"
build_dir="${artifact_root}/build"
mkdir -p "${artifact_root}/logs"

if ! command -v valgrind >/dev/null 2>&1; then
    printf 'valgrind tools: NOT_AVAILABLE (valgrind not installed)\n'
    exit 2
fi

cmake -S "${workspace}" -B "${build_dir}" -DCMAKE_BUILD_TYPE=Debug
cmake --build "${build_dir}" --target aisthra_test_core aisthra_test_concurrency -j"${CMAKE_BUILD_PARALLEL_LEVEL:-2}"
valgrind --tool=massif --massif-out-file="${artifact_root}/logs/massif.out" "${build_dir}/aisthra_test_core"
valgrind --tool=helgrind --log-file="${artifact_root}/logs/helgrind.log" "${build_dir}/aisthra_test_concurrency"
valgrind --tool=drd --log-file="${artifact_root}/logs/drd.log" "${build_dir}/aisthra_test_concurrency"
valgrind --tool=callgrind --callgrind-out-file="${artifact_root}/logs/callgrind.out" "${build_dir}/aisthra_test_core"
printf 'valgrind tools: PASS\n'
