#!/usr/bin/env bash
set -euo pipefail

build_dir="${1:?build directory is required}"
workspace="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
prefix="${build_dir}/aisthra-installed-prefix"
consumer_build="${build_dir}/aisthra-installed-consumer"

cmake --install "${build_dir}" --prefix "${prefix}"
cmake -S "${workspace}/tests/integration/install_consumer" \
    -B "${consumer_build}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="${prefix}" \
    -DAIS_USE_SHARED=ON
cmake --build "${consumer_build}" -j"${CMAKE_BUILD_PARALLEL_LEVEL:-2}"
"${consumer_build}/aisthra_installed_consumer"
printf 'installed consumer: PASS\n'
