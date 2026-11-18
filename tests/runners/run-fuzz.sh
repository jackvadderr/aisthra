#!/usr/bin/env bash
set -euo pipefail
root="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
if ! command -v clang >/dev/null 2>&1; then
    printf '%s\n' 'libFuzzer: NOT_AVAILABLE (clang missing)' >&2
    exit 2
fi
artifact_root="${AISTHRA_ARTIFACT_ROOT:-${root}/.artifacts}"
build_dir="${artifact_root}/fuzz"
work_corpus="${build_dir}/corpus-$(date +%Y%m%d-%H%M%S)-$$"
mkdir -p "$build_dir"
mkdir -p "$work_corpus"
cp -a "$root/tests/fuzz/corpus/." "$work_corpus/"
clang -std=c17 -O1 -g -fno-omit-frame-pointer -fsanitize=fuzzer,address,undefined \
    -pthread -I"$root/include" "$root/src/aisthra.c" \
    "$root/tests/fuzz/validate_observation_fuzz.c" -lm -o "$build_dir/validate_observation_fuzz"
"$build_dir/validate_observation_fuzz" "$work_corpus" \
    -max_total_time="${AISTHRA_FUZZ_SECONDS:-5}" -artifact_prefix="$build_dir/artifact-"
printf '%s\n' 'libFuzzer smoke: PASS'
