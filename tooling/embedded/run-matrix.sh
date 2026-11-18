#!/usr/bin/env bash
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
artifact_root="${AISTHRA_EMBEDDED_ARTIFACT_ROOT:-${root}/.artifacts/embedded}"
result_dir="${AISTHRA_EMBEDDED_RESULT_DIR:-${artifact_root}/results}"
matrix_file="${AISTHRA_EMBEDDED_MATRIX_OUTPUT:-${result_dir}/matrix.json}"
python_bin="${AISTHRA_PYTHON:-python3}"
overall=0
result_files=()

mkdir -p "$result_dir"

tool_version()
{
    local tool="$1"
    local path
    if [[ "$tool" == /* && -x "$tool" ]]; then
        path="$tool"
    else
        case "$tool" in
            xtensa-esp-elf-gcc|riscv32-esp-elf-gcc|qemu-system-xtensa|qemu-system-riscv32)
                path="$(find "${IDF_TOOLS_PATH:-}" -type f -name "$tool" -print -quit 2>/dev/null)"
                ;;
            arm-zephyr-eabi-gcc|riscv64-zephyr-elf-gcc|qemu-system-arm)
                path="$(find "${ZEPHYR_SDK_INSTALL_DIR:-}" -type f -name "$tool" -print -quit 2>/dev/null)"
                ;;
            *)
                path=""
                ;;
        esac
    fi
    if [[ -n "$path" && -x "$path" ]]; then
        "$path" --version 2>/dev/null | head -n 1 || true
    elif command -v "$tool" >/dev/null 2>&1; then
        "$tool" --version 2>/dev/null | head -n 1 || true
    fi
}

status_for_exit()
{
    case "$1" in
        0) printf '%s\n' PASS ;;
        2) printf '%s\n' BLOCKED ;;
        *) printf '%s\n' FAIL ;;
    esac
}

run_target()
{
    local kind="$1"
    local target="$2"
    local runner
    local run_log
    local build_log
    local status
    local rc
    local architecture
    local framework
    local framework_version
    local compiler
    local compiler_version
    local qemu_bin
    local qemu_version
    local output="${result_dir}/${target}.json"

    if [[ "$kind" == "esp" ]]; then
        runner="${root}/tooling/embedded/run-qemu.sh"
        run_log="${artifact_root}/logs/${target}-qemu.log"
        build_log="${artifact_root}/logs/${target}-build.log"
        framework="ESP-IDF"
        framework_version="ESP-IDF 6.1.1"
        case "$target" in
            esp32) architecture="Xtensa LX6" ;;
            esp32s3) architecture="Xtensa LX7" ;;
            esp32c3) architecture="RISC-V RV32IMC" ;;
        esac
        if [[ "$target" == "esp32c3" ]]; then
            compiler="riscv32-esp-elf-gcc"
            qemu_bin="qemu-system-riscv32"
        else
            compiler="xtensa-esp-elf-gcc"
            qemu_bin="qemu-system-xtensa"
        fi
    else
        runner="${root}/tooling/embedded/run-zephyr.sh"
        run_log="${artifact_root}/logs/${target}-zephyr-qemu.log"
        build_log="${artifact_root}/logs/${target}-zephyr-build.log"
        framework="Zephyr"
        framework_version="Zephyr 4.5.0"
        if [[ "$target" == "qemu_cortex_m3" ]]; then
            architecture="Armv7-M Cortex-M3"
            compiler="arm-zephyr-eabi-gcc"
            qemu_bin="${ZEPHYR_SDK_INSTALL_DIR:-}/hosttools/sysroots/x86_64-pokysdk-linux/usr/bin/qemu-system-arm"
            [[ -x "$qemu_bin" ]] || qemu_bin="qemu-system-arm"
        else
            architecture="RISC-V 32-bit"
            compiler="riscv64-zephyr-elf-gcc"
            qemu_bin="${ZEPHYR_SDK_INSTALL_DIR:-}/hosttools/sysroots/x86_64-pokysdk-linux/usr/bin/qemu-system-riscv32"
            [[ -x "$qemu_bin" ]] || qemu_bin="qemu-system-riscv32"
        fi
    fi

    compiler_version="$(tool_version "$compiler")"
    qemu_version="$(tool_version "$qemu_bin")"
    set +e
    "$runner" "$target"
    rc=$?
    set -e
    status="$(status_for_exit "$rc")"
    if [[ "$rc" -ne 0 ]]; then
        overall=1
    fi

    "$python_bin" "${root}/tooling/embedded/emit-result.py" \
        --target "$target" \
        --log "$run_log" \
        --build-log "$build_log" \
        --output "$output" \
        --status "$status" \
        --exit-code "$rc" \
        --architecture "$architecture" \
        --execution-environment QEMU \
        --framework "$framework" \
        --framework-version "$framework_version" \
        --compiler "$compiler" \
        --compiler-version "$compiler_version" \
        --qemu-version "$qemu_version" \
        --note "Raw run log: ${run_log}" \
        --note "QEMU measurements are diagnostic for this target/configuration and are not cross-target CPU comparisons."
    result_files+=("$output")
}

for target in esp32 esp32s3 esp32c3; do
    run_target esp "$target"
done
for target in qemu_cortex_m3 qemu_riscv32; do
    run_target zephyr "$target"
done

"$python_bin" - "$matrix_file" "${result_files[@]}" <<'PY'
import datetime as dt
import json
import pathlib
import sys

output = pathlib.Path(sys.argv[1])
targets = [json.loads(pathlib.Path(item).read_text(encoding="utf-8"))
           for item in sys.argv[2:]]
statuses = [item["status"] for item in targets]
if statuses and all(item == "PASS" for item in statuses):
    status = "PASS"
elif any(item == "PASS" for item in statuses):
    status = "PARTIAL"
elif any(item == "BLOCKED" for item in statuses):
    status = "BLOCKED"
else:
    status = "FAIL"
payload = {
    "schema": "aisthra-embedded-matrix-v1",
    "generated_at": dt.datetime.now(dt.timezone.utc).isoformat(),
    "status": status,
    "targets": targets,
}
output.parent.mkdir(parents=True, exist_ok=True)
output.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n",
                  encoding="utf-8")
print(f"AISTHRA_EMBEDDED_MATRIX status={status} output={output}")
PY

exit "$overall"
