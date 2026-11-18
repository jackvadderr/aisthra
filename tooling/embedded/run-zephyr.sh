#!/usr/bin/env bash
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
project="${root}/examples/zephyr"
artifact_root="${AISTHRA_EMBEDDED_ARTIFACT_ROOT:-${root}/.artifacts/embedded}"
workspace="${AISTHRA_ZEPHYR_WORKSPACE:-/tmp/aisthra-zephyr}"
board="${AISTHRA_ZEPHYR_BOARD:-${1:-qemu_cortex_m3}}"
build_dir="${AISTHRA_ZEPHYR_BUILD_DIR:-${artifact_root}/${board}-zephyr}"
log_dir="${artifact_root}/logs"
build_log="${AISTHRA_ZEPHYR_BUILD_LOG:-${log_dir}/${board}-zephyr-build.log}"
qemu_log="${AISTHRA_ZEPHYR_QEMU_LOG:-${log_dir}/${board}-zephyr-qemu.log}"
timeout_seconds="${AISTHRA_ZEPHYR_TIMEOUT_SECONDS:-45}"

case "$board" in
    qemu_cortex_m3|qemu_riscv32)
        ;;
    *)
        printf '%s\n' "ZEPHYR[$board]: BLOCKED (unsupported board id)" >&2
        exit 2
        ;;
esac

if [[ -n "${AISTHRA_ZEPHYR_WEST:-}" ]]; then
    west_bin="$AISTHRA_ZEPHYR_WEST"
elif command -v west >/dev/null 2>&1; then
    west_bin="$(command -v west)"
elif [[ -x /tmp/aisthra-zephyr-venv/bin/west ]]; then
    west_bin="/tmp/aisthra-zephyr-venv/bin/west"
else
    printf '%s\n' "ZEPHYR[$board]: BLOCKED (install west or set AISTHRA_ZEPHYR_WEST)" >&2
    exit 2
fi

if [[ ! -d "${workspace}/zephyr" || ! -f "${workspace}/.west/config" ]]; then
    printf '%s\n' "ZEPHYR[$board]: BLOCKED (workspace is unavailable: $workspace)" >&2
    exit 2
fi

export ZEPHYR_BASE="${workspace}/zephyr"
mkdir -p "$log_dir" "$build_dir"

{
    printf 'AISTHRA_ZEPHYR_BUILD board=%s workspace=%s\n' "$board" "$workspace"
    "$west_bin" --version
    git -C "$ZEPHYR_BASE" describe --always --dirty
    "$west_bin" boards | rg -q "^${board}([[:space:]]|$)"
    "$west_bin" build --board "$board" --build-dir "$build_dir" "$project"
} >"$build_log" 2>&1 || {
    printf '%s\n' "ZEPHYR[$board]: FAIL (build; see $build_log)" >&2
    exit 1
}

set +e
# west's run target leaves QEMU attached after the firmware has emitted its
# final result. Give the run its own process group and stop it at that marker;
# the timeout remains the protection for a boot hang or missing output.
setsid sh -c \
    'cd "$1" && exec timeout --foreground "$2" "$3" build --build-dir "$4" -t run' \
    aisthra-zephyr-run "$workspace" "${timeout_seconds}s" "$west_bin" "$build_dir" \
    >"$qemu_log" 2>&1 &
run_pid=$!
set -e

while kill -0 "$run_pid" 2>/dev/null; do
    if [[ "$(rg -c "^AISTHRA_EMBEDDED_RESULT target=${board} status=PASS\r?$" "$qemu_log" || true)" == 1 ]]; then
        kill -TERM -- "-$run_pid" 2>/dev/null || true
        break
    fi
    sleep 1
done

if kill -0 "$run_pid" 2>/dev/null; then
    kill -TERM -- "-$run_pid" 2>/dev/null || true
fi
if wait "$run_pid"; then
    qemu_status=0
else
    qemu_status=$?
fi

if [[ "$(rg -c '^AISTHRA_CONTRACT:PASS\r?$' "$qemu_log" || true)" != 1 ||
      "$(rg -c "^AISTHRA_EMBEDDED_RESULT target=${board} status=PASS\r?$" "$qemu_log" || true)" != 1 ]]; then
    printf '%s\n' "ZEPHYR[$board]: FAIL (missing unique PASS markers; status=$qemu_status; see $qemu_log)" >&2
    exit 1
fi

if ! rg -q "^AISTHRA_EMBEDDED_CONFIG target=${board} " "$qemu_log" ||
   ! rg -q '^AISTHRA_EMBEDDED_HEAP_PROBE ' "$qemu_log" ||
   ! rg -q '^AISTHRA_EMBEDDED_STACK ' "$qemu_log" ||
   ! rg -q '^AISTHRA_EMBEDDED_METRIC ' "$qemu_log" ||
   ! rg -q '^AISTHRA_EMBEDDED_BENCH workload=context_create_close .*status=PASS' "$qemu_log" ||
   ! rg -q '^AISTHRA_EMBEDDED_BENCH workload=provider_register .*status=PASS' "$qemu_log" ||
   ! rg -q '^AISTHRA_EMBEDDED_BENCH workload=sensor_register .*status=PASS' "$qemu_log" ||
   ! rg -q '^AISTHRA_EMBEDDED_BENCH workload=validation .*status=PASS' "$qemu_log" ||
   ! rg -q '^AISTHRA_EMBEDDED_BENCH workload=publication .*status=PASS' "$qemu_log" ||
   ! rg -q '^AISTHRA_EMBEDDED_BENCH workload=latest .*status=PASS' "$qemu_log" ||
   ! rg -q '^AISTHRA_EMBEDDED_BENCH workload=snapshot .*status=PASS' "$qemu_log" ||
   ! rg -q '^AISTHRA_EMBEDDED_BENCH workload=refresh .*status=PASS' "$qemu_log" ||
   ! rg -q '^AISTHRA_EMBEDDED_BENCH workload=percept .*status=PASS' "$qemu_log" ||
   ! rg -q '^AISTHRA_EMBEDDED_BENCH workload=mixed .*status=PASS' "$qemu_log" ||
   rg -qi 'ASSERTION FAIL|HARD FAULT|USAGE FAULT|BUS FAULT|MEMMANAGE FAULT|KERNEL PANIC|fatal error|stack overflow|watchdog|^panic:' "$qemu_log"; then
    printf '%s\n' "ZEPHYR[$board]: FAIL (health checks failed; see $qemu_log)" >&2
    exit 1
fi

printf '%s\n' "ZEPHYR[$board]: PASS (emulated functional run; log: $qemu_log)"
