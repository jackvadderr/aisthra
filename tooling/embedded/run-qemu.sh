#!/usr/bin/env bash
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
project="${root}/examples/esp32/idf"
artifact_root="${AISTHRA_EMBEDDED_ARTIFACT_ROOT:-${root}/.artifacts/embedded}"
target="${AISTHRA_ESP_TARGET:-${1:-esp32}}"
if [[ "$target" == "esp32" && -n "${AISTHRA_ESP32_BUILD_DIR:-}" ]]; then
    build_dir="$AISTHRA_ESP32_BUILD_DIR"
else
    build_dir="${AISTHRA_ESP_BUILD_DIR:-${artifact_root}/${target}-idf}"
fi
log_dir="${artifact_root}/logs"
log_file="${AISTHRA_EMBEDDED_QEMU_LOG:-${log_dir}/${target}-qemu.log}"
# Espressif's unaccelerated Xtensa/RV32 QEMU can need more than one minute
# for the bounded public-contract and benchmark workload. Keep the timeout
# finite while leaving room for the slowest supported emulated target.
timeout_seconds="${AISTHRA_QEMU_TIMEOUT_SECONDS:-240}"
defaults="${project}/sdkconfig.defaults;${project}/sdkconfig.defaults.${target}"
cmake_config_arg=( -D "SDKCONFIG=${build_dir}/sdkconfig" )

mkdir -p "$log_dir"
"${root}/tooling/embedded/run-build.sh" "$target"

export IDF_PATH
export SDKCONFIG="${build_dir}/sdkconfig"
export SDKCONFIG_DEFAULTS="$defaults"
source "$IDF_PATH/export.sh" >>"$log_file" 2>&1
if ! source "${root}/tooling/embedded/esp-env.sh" ||
   ! aisthra_embedded_select_esp_tools "$target"; then
    printf '%s\n' "ESP_QEMU[$target]: BLOCKED (target QEMU is not installed; see $log_file)" >&2
    exit 2
fi

set +e
# idf.py's monitor intentionally stays attached after the firmware has
# printed its final marker. Run it in its own process group so the runner can
# stop both the monitor and its QEMU child as soon as the bounded run passes.
setsid timeout --foreground "${timeout_seconds}s" \
    idf.py "${cmake_config_arg[@]}" -C "$project" -B "$build_dir" qemu monitor \
    </dev/null >"$log_file" 2>&1 &
monitor_pid=$!
set -e

while kill -0 "$monitor_pid" 2>/dev/null; do
    if [[ "$(rg -c '^AISTHRA_EMBEDDED_SMOKE:PASS\r?$' "$log_file" || true)" == 1 ]]; then
        kill -TERM -- "-$monitor_pid" 2>/dev/null || true
        break
    fi
    sleep 1
done

if kill -0 "$monitor_pid" 2>/dev/null; then
    kill -TERM -- "-$monitor_pid" 2>/dev/null || true
fi
if wait "$monitor_pid"; then
    qemu_status=0
else
    qemu_status=$?
fi

if [[ "$(rg -c '^AISTHRA_EMBEDDED_SMOKE:PASS\r?$' "$log_file" || true)" != 1 ]]; then
    printf '%s\n' "ESP_QEMU[$target]: FAIL (missing unique PASS marker; status=$qemu_status; see $log_file)" >&2
    exit 1
fi

if [[ "$(rg -c '^AISTHRA_CONTRACT:PASS\r?$' "$log_file" || true)" != 1 ]]; then
    printf '%s\n' "ESP_QEMU[$target]: FAIL (public-contract suite did not pass; status=$qemu_status; see $log_file)" >&2
    exit 1
fi

if [[ "$(rg -c "^AISTHRA_EMBEDDED_RESULT target=${target} status=PASS\\r?$" "$log_file" || true)" != 1 ]]; then
    printf '%s\n' "ESP_QEMU[$target]: FAIL (result marker did not pass; status=$qemu_status; see $log_file)" >&2
    exit 1
fi

if ! rg -q "^AISTHRA_EMBEDDED_CONFIG target=${target} " "$log_file" ||
   ! rg -q '^AISTHRA_EMBEDDED_HEAP_PROBE ' "$log_file" ||
   ! rg -q '^AISTHRA_EMBEDDED_HEAP checkpoint=after-close ' "$log_file" ||
   ! rg -q '^AISTHRA_EMBEDDED_METRIC ' "$log_file" ||
   ! rg -q '^AISTHRA_EMBEDDED_BENCH workload=context_create_close .*status=PASS' "$log_file" ||
   ! rg -q '^AISTHRA_EMBEDDED_BENCH workload=provider_register .*status=PASS' "$log_file" ||
   ! rg -q '^AISTHRA_EMBEDDED_BENCH workload=sensor_register .*status=PASS' "$log_file" ||
   ! rg -q '^AISTHRA_EMBEDDED_BENCH workload=validation .*status=PASS' "$log_file" ||
   ! rg -q '^AISTHRA_EMBEDDED_BENCH workload=publication .*status=PASS' "$log_file" ||
   ! rg -q '^AISTHRA_EMBEDDED_BENCH workload=latest .*status=PASS' "$log_file" ||
   ! rg -q '^AISTHRA_EMBEDDED_BENCH workload=snapshot .*status=PASS' "$log_file" ||
   ! rg -q '^AISTHRA_EMBEDDED_BENCH workload=refresh .*status=PASS' "$log_file" ||
   ! rg -q '^AISTHRA_EMBEDDED_BENCH workload=percept .*status=PASS' "$log_file" ||
   ! rg -q '^AISTHRA_EMBEDDED_BENCH workload=mixed .*status=PASS' "$log_file" ||
   rg -qi 'Guru Meditation|abort\(\) was called|assert failed|watchdog|stack smashing|^panic:' "$log_file"; then
    printf '%s\n' "ESP_QEMU[$target]: FAIL (health checks failed; see $log_file)" >&2
    exit 1
fi

printf '%s\n' "ESP_QEMU[$target]: PASS (emulated functional run; log: $log_file)"
