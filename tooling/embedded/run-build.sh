#!/usr/bin/env bash
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
project="${root}/examples/esp32/idf"
artifact_root="${AISTHRA_EMBEDDED_ARTIFACT_ROOT:-${root}/.artifacts/embedded}"
target="${AISTHRA_ESP_TARGET:-${1:-esp32}}"

case "$target" in
    esp32|esp32s3|esp32c3)
        ;;
    *)
        printf '%s\n' "ESP_BUILD: BLOCKED (unsupported target: $target)" >&2
        exit 2
        ;;
esac

if [[ "$target" == "esp32" && -n "${AISTHRA_ESP32_BUILD_DIR:-}" ]]; then
    build_dir="$AISTHRA_ESP32_BUILD_DIR"
else
    build_dir="${AISTHRA_ESP_BUILD_DIR:-${artifact_root}/${target}-idf}"
fi
log_dir="${artifact_root}/logs"
log_file="${AISTHRA_EMBEDDED_BUILD_LOG:-${log_dir}/${target}-build.log}"
sdkconfig_file="${build_dir}/sdkconfig"
defaults="${project}/sdkconfig.defaults;${project}/sdkconfig.defaults.${target}"
cmake_config_arg=( -D "SDKCONFIG=${sdkconfig_file}" )

if [[ -z "${IDF_PATH:-}" || ! -f "${IDF_PATH}/export.sh" ]]; then
    printf '%s\n' "ESP_BUILD[$target]: BLOCKED (set IDF_PATH to an ESP-IDF checkout)" >&2
    exit 2
fi

mkdir -p "$log_dir" "$build_dir"
export IDF_PATH
# The project uses only the ESP-IDF components required by the selected target.
# Keep optional checkout validation from pulling unrelated submodules into a
# reproducible, isolated validation run.
export IDF_SKIP_CHECK_SUBMODULES="${IDF_SKIP_CHECK_SUBMODULES:-1}"
source "$IDF_PATH/export.sh" >>"$log_file" 2>&1
if ! source "${root}/tooling/embedded/esp-env.sh" ||
   ! aisthra_embedded_select_esp_tools "$target"; then
    printf '%s\n' "ESP_BUILD[$target]: BLOCKED (target toolchain is not installed; see $log_file)" >&2
    exit 2
fi
export SDKCONFIG="$sdkconfig_file"
export SDKCONFIG_DEFAULTS="$defaults"

{
    printf '\nAISTHRA_EMBEDDED_BUILD target=%s sdkconfig=%s\n' "$target" "$sdkconfig_file"
    idf.py --version
    if [[ "$target" == "esp32c3" ]]; then
        compiler_probe="riscv32-esp-elf-gcc"
    else
        compiler_probe="xtensa-esp-elf-gcc"
    fi
    if command -v "$compiler_probe" >/dev/null 2>&1; then
        "$compiler_probe" --version | head -n 1
    fi
} >>"$log_file" 2>&1 || {
    printf '%s\n' "ESP_BUILD[$target]: BLOCKED (ESP-IDF environment is unavailable; see $log_file)" >&2
    exit 2
}

needs_set_target=0
if [[ "${AISTHRA_FORCE_SET_TARGET:-0}" == "1" ||
      ! -f "${build_dir}/CMakeCache.txt" ]]; then
    needs_set_target=1
elif ! rg -q "IDF_TARGET:[^=]*=${target}" "${build_dir}/CMakeCache.txt"; then
    needs_set_target=1
fi

if [[ "$needs_set_target" == "1" ]]; then
    if ! idf.py "${cmake_config_arg[@]}" -C "$project" -B "$build_dir" \
        set-target "$target" >>"$log_file" 2>&1; then
        printf '%s\n' "ESP_BUILD[$target]: FAIL (set-target; see $log_file)" >&2
        exit 1
    fi
else
    printf '%s\n' "ESP_BUILD[$target]: REUSE (existing CMake build)" >>"$log_file"
fi

if ! idf.py "${cmake_config_arg[@]}" -C "$project" -B "$build_dir" \
    build >>"$log_file" 2>&1; then
    printf '%s\n' "ESP_BUILD[$target]: FAIL (build; see $log_file)" >&2
    exit 1
fi

if ! idf.py "${cmake_config_arg[@]}" -C "$project" -B "$build_dir" \
    size >>"$log_file" 2>&1; then
    printf '%s\n' "AISTHRA_EMBEDDED_BUILD_SIZE target=$target status=NOT_AVAILABLE" >>"$log_file"
fi

printf '%s\n' "ESP_BUILD[$target]: PASS (log: $log_file)"
