#!/usr/bin/env bash
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
artifact_root="${AISTHRA_ARDUINO_ARTIFACT_ROOT:-${root}/.artifacts/arduino}"
result_dir="${AISTHRA_ARDUINO_RESULT_DIR:-${artifact_root}/results}"
matrix_file="${AISTHRA_ARDUINO_MATRIX_OUTPUT:-${result_dir}/matrix.json}"
log_root="${AISTHRA_ARDUINO_LOG_ROOT:-${artifact_root}/logs}"
python_bin="${AISTHRA_PYTHON:-python3}"
runner="${root}/tooling/embedded/run-arduino.sh"
overall=0
passed=0
unavailable=0
result_files=()

mkdir -p "$result_dir" "$log_root"

run_target()
{
    local target="$1"
    local architecture="$2"
    local runner_log="${log_root}/${target}-runner.log"
    local build_log="${log_root}/${target}-build.log"
    local output="${result_dir}/${target}.json"
    local rc

    set +e
    "$runner" "$target" >"$runner_log" 2>&1
    rc=$?
    set -e

    case "$rc" in
        0) passed=$((passed + 1)) ;;
        2) unavailable=$((unavailable + 1)) ;;
        *) overall=1 ;;
    esac

    "$python_bin" "${root}/tooling/embedded/emit-arduino-result.py" \
        --target "$target" \
        --architecture "$architecture" \
        --runner-log "$runner_log" \
        --build-log "$build_log" \
        --output "$output" \
        --status "$(case "$rc" in 0) printf PASS ;; 2) printf NOT_AVAILABLE ;; *) printf FAIL ;; esac)" \
        --exit-code "$rc"
    result_files+=("$output")
}

run_target esp32 "Xtensa LX6"
run_target esp32s3 "Xtensa LX7"
run_target esp32c3 "RISC-V RV32IMC"

if [[ "$overall" -eq 0 ]]; then
    if [[ "$passed" -eq 3 ]]; then
        overall=0
    elif [[ "$unavailable" -eq 3 ]]; then
        overall=2
    else
        overall=1
    fi
fi

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
    status = "BUILD_VALIDATED"
elif statuses and all(item == "NOT_AVAILABLE" for item in statuses):
    status = "NOT_AVAILABLE"
elif any(item == "PASS" for item in statuses):
    status = "PARTIAL"
else:
    status = "FAIL"
payload = {
    "schema": "aisthra-arduino-matrix-v1",
    "generated_at": dt.datetime.now(dt.timezone.utc).isoformat(),
    "status": status,
    "classification": "BUILD_VALIDATED" if status == "BUILD_VALIDATED" else status,
    "qemu_status": "NOT_TESTED",
    "targets": targets,
}
output.parent.mkdir(parents=True, exist_ok=True)
output.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n",
                  encoding="utf-8")
print(f"AISTHRA_ARDUINO_MATRIX status={status} output={output}")
PY

exit "$overall"
