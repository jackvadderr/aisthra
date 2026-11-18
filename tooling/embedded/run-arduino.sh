#!/usr/bin/env bash
set -euo pipefail

# Build the Arduino-ESP32 compatibility sketch without changing the user's
# global Arduino installation and without uploading to a physical board.

root="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
target="${1:-esp32}"
artifact_root="${AISTHRA_ARDUINO_ARTIFACT_ROOT:-${root}/.artifacts/arduino}"
config_dir="${AISTHRA_ARDUINO_CONFIG_DIR:-${artifact_root}/config}"
user_dir="${AISTHRA_ARDUINO_USER_DIR:-${artifact_root}/user}"
work_root="${AISTHRA_ARDUINO_WORK_ROOT:-${artifact_root}/work}"
build_root="${AISTHRA_ARDUINO_BUILD_ROOT:-${artifact_root}/build}"
log_root="${AISTHRA_ARDUINO_LOG_ROOT:-${artifact_root}/logs}"
additional_url="${AISTHRA_ARDUINO_ADDITIONAL_URL:-https://espressif.github.io/arduino-esp32/package_esp32_index.json}"
core_version="${AISTHRA_ARDUINO_CORE_VERSION:-}"
install_core="${AISTHRA_ARDUINO_INSTALL_CORE:-0}"

case "$target" in
    esp32) default_fqbn="esp32:esp32:esp32" ;;
    esp32s3) default_fqbn="esp32:esp32:esp32s3" ;;
    esp32c3) default_fqbn="esp32:esp32:esp32c3" ;;
    *)
        printf 'ARDUINO_BUILD: NOT_AVAILABLE target=%s reason=unsupported-target\n' "$target"
        printf 'ARDUINO_QEMU: NOT_TESTED target=%s reason=arduino-firmware-not-routed-to-qemu\n' "$target"
        exit 2
        ;;
esac
fqbn="${AISTHRA_ARDUINO_FQBN:-$default_fqbn}"

arduino_cli="${AISTHRA_ARDUINO_CLI:-}"
if [[ -z "$arduino_cli" ]]; then
    arduino_cli="$(command -v arduino-cli || true)"
fi
if [[ -z "$arduino_cli" || ! -x "$arduino_cli" ]]; then
    printf 'ARDUINO_BUILD: NOT_AVAILABLE target=%s reason=arduino-cli-missing\n' "$target"
    printf 'ARDUINO_QEMU: NOT_TESTED target=%s reason=arduino-firmware-not-routed-to-qemu\n' "$target"
    exit 2
fi

mkdir -p "$config_dir" "$user_dir" "$work_root" "$build_root" "$log_root"
if [[ ! -f "$config_dir/arduino-cli.yaml" ]]; then
    "$arduino_cli" config init --config-dir "$config_dir" >/dev/null
fi

cli()
{
    "$arduino_cli" --config-dir "$config_dir" \
        --additional-urls "$additional_url" "$@"
}

# Keep the package cache and installed core in the caller-selected local data
# directory.  This is deliberately not the user's global Arduino directory.
cli config set directories.data "$user_dir" >/dev/null

core_list="$(cli core list 2>&1 || true)"
if ! printf '%s\n' "$core_list" | rg -q '^[[:space:]]*esp32:esp32[[:space:]]'; then
    if [[ "$install_core" != 1 ]]; then
        printf 'ARDUINO_BUILD: NOT_AVAILABLE target=%s reason=esp32-core-missing\n' "$target"
        printf 'ARDUINO_QEMU: NOT_TESTED target=%s reason=arduino-firmware-not-routed-to-qemu\n' "$target"
        exit 2
    fi
    core_spec="esp32:esp32"
    if [[ -n "$core_version" ]]; then
        core_spec="${core_spec}@${core_version}"
    fi
    if ! cli core install "$core_spec"; then
        printf 'ARDUINO_BUILD: NOT_AVAILABLE target=%s reason=esp32-core-install-failed\n' "$target"
        printf 'ARDUINO_QEMU: NOT_TESTED target=%s reason=arduino-firmware-not-routed-to-qemu\n' "$target"
        exit 2
    fi
    core_list="$(cli core list 2>&1 || true)"
fi

if ! printf '%s\n' "$core_list" | rg -q '^[[:space:]]*esp32:esp32[[:space:]]'; then
    printf 'ARDUINO_BUILD: NOT_AVAILABLE target=%s reason=esp32-core-not-visible\n' "$target"
    printf 'ARDUINO_QEMU: NOT_TESTED target=%s reason=arduino-firmware-not-routed-to-qemu\n' "$target"
    exit 2
fi

board_list="$(cli board listall esp32:esp32 2>&1 || true)"
if ! printf '%s\n' "$board_list" | rg -q "(^|[[:space:]])${fqbn//:/\\:}([[:space:]]|$)"; then
    printf 'ARDUINO_BUILD: FAIL target=%s reason=fqbn-not-advertised fqbn=%s\n' "$target" "$fqbn"
    printf 'ARDUINO_QEMU: NOT_TESTED target=%s reason=arduino-firmware-not-routed-to-qemu\n' "$target"
    exit 1
fi

source_sketch="${root}/examples/arduino/AisthraSmoke"
if [[ ! -f "$source_sketch/AisthraSmoke.ino" ]]; then
    printf 'ARDUINO_BUILD: FAIL target=%s reason=source-sketch-missing\n' "$target"
    printf 'ARDUINO_QEMU: NOT_TESTED target=%s reason=arduino-firmware-not-routed-to-qemu\n' "$target"
    exit 1
fi

work_parent="$(mktemp -d "$work_root/${target}.XXXXXX")"
work_sketch="$work_parent/AisthraSmoke"
mkdir -p "$work_sketch/src"
cp "$source_sketch/AisthraSmoke.ino" "$work_sketch/AisthraSmoke.ino"
cp "$root/src/aisthra.c" "$work_sketch/src/aisthra.c"
cp -a "$root/src/internal" "$work_sketch/src/"
cp -a "$root/include/aisthra" "$work_sketch/src/"
# Arduino CLI compiles sketch sources recursively but does not promise that
# `src/` is an include search root for the sketch's C++ translation unit. Keep
# the same public headers at the sketch root so both the .ino and aisthra.c
# resolve the canonical <aisthra/...> include form.
cp -a "$root/include/aisthra" "$work_sketch/"

# The generated sketch is a temporary consumer view of the original sources.
# Fail before compilation if any copied source or private adapter diverges.
if ! {
    cmp "$source_sketch/AisthraSmoke.ino" "$work_sketch/AisthraSmoke.ino"
    cmp "$root/src/aisthra.c" "$work_sketch/src/aisthra.c"
    diff -ru "$root/src/internal" "$work_sketch/src/internal"
    diff -ru "$root/include/aisthra" "$work_sketch/src/aisthra"
    diff -ru "$root/include/aisthra" "$work_sketch/aisthra"
}; then
    printf 'ARDUINO_BUILD: FAIL target=%s reason=source-copy-mismatch\n' "$target"
    printf 'ARDUINO_QEMU: NOT_TESTED target=%s reason=arduino-firmware-not-routed-to-qemu\n' "$target"
    exit 1
fi
printf 'ARDUINO_SOURCE_COPY: PASS target=%s sketch=%s\n' "$target" "$work_sketch"
sha256sum \
    "$source_sketch/AisthraSmoke.ino" \
    "$root/src/aisthra.c" \
    "$root/src/internal/aisthra_platform.h" \
    "$work_sketch/AisthraSmoke.ino" \
    "$work_sketch/src/aisthra.c" \
    "$work_sketch/src/internal/aisthra_platform.h" \
    >"$work_parent/source-hashes.sha256"

build_dir="${build_root}/${target}"
output_dir="${build_dir}/output"
log_file="${log_root}/${target}-build.log"
mkdir -p "$build_dir" "$output_dir"

set +e
cli compile \
    --fqbn "$fqbn" \
    --warnings all \
    --clean \
    --build-path "$build_dir" \
    --output-dir "$output_dir" \
    "$work_sketch" >"$log_file" 2>&1
compile_rc=$?
set -e

core_version_installed="$(printf '%s\n' "$core_list" | awk '$1 == "esp32:esp32" { print $2; exit }')"
cli_version="$($arduino_cli version 2>/dev/null | awk '/Version:/{for (i = 1; i <= NF; ++i) if ($i == "Version:") { print $(i + 1); exit }}' || true)"
artifact="${output_dir}/AisthraSmoke.ino.bin"
if [[ ! -f "$artifact" ]]; then
    artifact="$(find "$build_dir" -type f -name '*.elf' -print -quit)"
fi
markers_ok=1
rg -q 'Sketch uses' "$log_file" || markers_ok=0
rg -q 'Global variables use' "$log_file" || markers_ok=0
if [[ "$compile_rc" -ne 0 || -z "$artifact" || "$markers_ok" -ne 1 ]]; then
    printf 'ARDUINO_BUILD: FAIL target=%s fqbn=%s core=%s cli=%s log=%s\n' \
        "$target" "$fqbn" "${core_version_installed:-unknown}" \
        "${cli_version:-unknown}" "$log_file"
    printf 'ARDUINO_QEMU: NOT_TESTED target=%s reason=arduino-firmware-not-routed-to-qemu\n' "$target"
    exit 1
fi

printf 'ARDUINO_BUILD: PASS target=%s fqbn=%s core=%s cli=%s artifact=%s\n' \
    "$target" "$fqbn" "${core_version_installed:-unknown}" \
    "${cli_version:-unknown}" "$artifact"
printf 'ARDUINO_QEMU: NOT_TESTED target=%s reason=arduino-firmware-not-routed-to-qemu\n' "$target"
