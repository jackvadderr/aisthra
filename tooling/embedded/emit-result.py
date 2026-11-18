#!/usr/bin/env python3
"""Convert an embedded build/run log into the A.13 result schema."""

from __future__ import annotations

import argparse
import datetime as dt
import json
import pathlib
import re
from typing import Any


STATUSES = {
    "PASS",
    "FAIL",
    "BLOCKED",
    "NOT_AVAILABLE",
    "NOT_RUN",
    "PARTIAL",
    "NOT_APPLICABLE",
}

ARCHITECTURES = {
    "esp32": "Xtensa LX6",
    "esp32s3": "Xtensa LX7",
    "esp32c3": "RISC-V RV32IMC",
    "qemu_cortex_m3": "Armv7-M Cortex-M3",
    "qemu_riscv32": "RISC-V 32-bit",
}


def _match(pattern: str, text: str, group: str | int = 1) -> str | None:
    found = re.search(pattern, text, re.MULTILINE)
    return found.group(group) if found else None


def _integer(value: str | None) -> int | None:
    if value is None:
        return None
    try:
        return int(value)
    except ValueError:
        return None


def _number(value: str | None) -> float | None:
    if value is None:
        return None
    try:
        return float(value)
    except ValueError:
        return None


def _last_int(pattern: str, text: str, group: str | int = 1) -> int | None:
    values = re.findall(pattern, text, re.MULTILINE)
    if not values:
        return None
    value = values[-1]
    if isinstance(value, tuple):
        value = value[group - 1 if isinstance(group, int) else 0]
    return _integer(value)


def _last_match(pattern: str, text: str) -> re.Match[str] | None:
    values = list(re.finditer(pattern, text, re.MULTILINE))
    return values[-1] if values else None


def _load(path: pathlib.Path | None) -> str:
    if path is None or not path.is_file():
        return ""
    return path.read_text(encoding="utf-8", errors="replace")


def _metadata(args: argparse.Namespace) -> dict[str, Any]:
    return {
        "target": args.target,
        "architecture": args.architecture or ARCHITECTURES.get(args.target),
        "execution_environment": args.execution_environment,
        "framework": args.framework,
        "framework_version": args.framework_version or None,
        "compiler": args.compiler or None,
        "compiler_version": args.compiler_version or None,
        "qemu_version": args.qemu_version or None,
        "configuration_id": args.configuration_id or f"embedded-{args.target}",
    }


def _benchmarks(text: str) -> dict[str, dict[str, Any]]:
    values: dict[str, dict[str, Any]] = {}
    pattern = re.compile(
        r"^AISTHRA_EMBEDDED_BENCH workload=(?P<workload>\S+) "
        r"operations=(?P<operations>\d+) elapsed_us=(?P<elapsed>\S+) "
        r"avg_us=(?P<avg>\S+) "
        r"throughput_ops_s=(?P<throughput>\S+) "
        r"status=(?P<status>\S+)$",
        re.MULTILINE,
    )
    for found in pattern.finditer(text):
        values[found.group("workload")] = {
            "operations": _integer(found.group("operations")),
            "elapsed_us": _number(found.group("elapsed")),
            "avg_us": _number(found.group("avg")),
            "throughput_ops_s": _number(found.group("throughput")),
            "status": found.group("status"),
        }
    return values


def build_result(args: argparse.Namespace) -> dict[str, Any]:
    run_text = _load(args.log)
    build_text = _load(args.build_log)
    text = f"{build_text}\n{run_text}"
    status = args.status if args.status in STATUSES else "NOT_RUN"
    config_target = _match(r"^AISTHRA_EMBEDDED_CONFIG target=(\S+)", run_text)
    heap_matches = list(
        re.finditer(
            r"^AISTHRA_EMBEDDED_HEAP checkpoint=(\S+) free=(\d+) "
            r"minimum=(\d+) largest=(\d+)$",
            run_text,
            re.MULTILINE,
        )
    )
    heap_by_checkpoint = {
        found.group(1): {
            "free_bytes": _integer(found.group(2)),
            "minimum_free_bytes": _integer(found.group(3)),
            "largest_free_block_bytes": _integer(found.group(4)),
        }
        for found in heap_matches
    }
    stack_matches = list(
        re.finditer(
            r"^AISTHRA_EMBEDDED_STACK checkpoint=(\S+) high_water_bytes=(\d+)$",
            run_text,
            re.MULTILINE,
        )
    )
    stack_by_checkpoint = {
        found.group(1): _integer(found.group(2)) for found in stack_matches
    }
    zephyr_stack = _last_int(
        r"^AISTHRA_EMBEDDED_STACK target=main unused_bytes=(\d+) status=",
        run_text,
    )
    heap_probe = _last_match(
        r"^AISTHRA_EMBEDDED_HEAP_PROBE (?P<fields>.*)$", run_text
    )
    probe_fields: dict[str, Any] = {}
    if heap_probe:
        for name, value in re.findall(
            r"(arena_bytes|allocated_bytes|largest_before|fragmented_largest)=(\d+)",
            heap_probe.group("fields"),
        ):
            probe_fields[name] = _integer(value)
        oom = _match(r"\boom=(observed|not-observed)", heap_probe.group("fields"))
        probe_fields["oom_observed"] = None if oom is None else oom == "observed"

    metric = _last_match(
        r"^AISTHRA_EMBEDDED_METRIC workload=(\S+) count=(\d+) "
        r"elapsed_us=([-+0-9.]+) avg_us=([-+0-9.]+)$",
        run_text,
    )
    if metric:
        metric_values: dict[str, Any] = {
            "workload": metric.group(1),
            "operations": _integer(metric.group(2)),
            "elapsed_us": _number(metric.group(3)),
            "avg_us": _number(metric.group(4)),
        }
    else:
        zephyr_metric = _last_match(
            r"^AISTHRA_EMBEDDED_METRIC workload=(\S+) elapsed_ms=(\S+) "
            r"status=(\S+)$",
            run_text,
        )
        zephyr_elapsed_ms = (
            _number(zephyr_metric.group(2)) if zephyr_metric else None
        )
        metric_values = (
            {
                "workload": zephyr_metric.group(1),
                "operations": None,
                "elapsed_us": (
                    None
                    if zephyr_elapsed_ms is None
                    else zephyr_elapsed_ms * 1000.0
                ),
                "avg_us": None,
                "status": zephyr_metric.group(3),
            }
            if zephyr_metric
            else {
                "workload": None,
                "operations": None,
                "elapsed_us": None,
                "avg_us": None,
                "status": None,
            }
        )
    firmware_bytes = _integer(
        _match(r"Total image size:\s*(\d+) bytes", build_text)
        or _match(r"binary size[=:]\s*(\d+) bytes", build_text, 1)
    )
    benchmarks = _benchmarks(run_text)
    smoke_status = status if status == "PASS" else status
    profiles = {
        "smoke": {
            "status": smoke_status,
            "operations": metric_values["operations"],
            "latency_us": metric_values["avg_us"],
            "throughput_ops_s": (
                None
                if metric_values["avg_us"] in (None, 0.0)
                else 1000000.0 / metric_values["avg_us"]
            ),
        },
        "small": {"status": "NOT_RUN"},
        "medium": {"status": "NOT_RUN"},
        "stress": {"status": "NOT_RUN"},
        "endurance": {"status": "NOT_RUN"},
    }
    result: dict[str, Any] = {
        "schema": "aisthra-embedded-result-v1",
        "generated_at": dt.datetime.now(dt.timezone.utc).isoformat(),
        **_metadata(args),
        "status": status,
        "run_exit_code": args.exit_code,
        "markers": {
            "config_target": config_target,
            "contract_pass_count": len(re.findall(r"^AISTHRA_CONTRACT:PASS$", run_text, re.MULTILINE)),
            "smoke_pass_count": len(re.findall(r"^AISTHRA_EMBEDDED_SMOKE:PASS$", run_text, re.MULTILINE)),
            "result_pass_count": len(re.findall(r"^AISTHRA_EMBEDDED_RESULT .* status=PASS$", run_text, re.MULTILINE)),
            "fault_detected": bool(
                re.search(
                    r"Guru Meditation|assert failed|watchdog|stack smashing|"
                    r"HARD FAULT|KERNEL PANIC|fatal error|stack overflow|"
                    r"abort\(\) was called|^panic:",
                    run_text,
                    re.IGNORECASE,
                )
            ),
        },
        "abi": {
            "pointer_bytes": _integer(_match(r"AISTHRA_EMBEDDED_ABI pointer=(\d+)", run_text)),
            "size_t_bytes": _integer(_match(r"AISTHRA_EMBEDDED_ABI .* size_t=(\d+)", run_text)),
            "long_bytes": _integer(_match(r"AISTHRA_EMBEDDED_ABI .* long=(\d+)", run_text)),
            "uint64_bytes": _integer(_match(r"AISTHRA_EMBEDDED_ABI .* uint64=(\d+)", run_text)),
        },
        "memory": {
            "heap_checkpoints": heap_by_checkpoint,
            "minimum_free_bytes": (
                heap_by_checkpoint.get("after-close", {}).get("minimum_free_bytes")
                if heap_by_checkpoint
                else None
            ),
            "largest_free_block_bytes": (
                heap_by_checkpoint.get("after-close", {}).get("largest_free_block_bytes")
                if heap_by_checkpoint
                else None
            ),
            "stack_high_water_bytes": (
                stack_by_checkpoint.get("after-close")
                or stack_by_checkpoint.get("after-stress")
                or zephyr_stack
            ),
            "heap_pressure": probe_fields,
            "firmware_bytes": firmware_bytes,
        },
        "metric": metric_values,
        "benchmarks": benchmarks,
        "profiles": profiles,
        "notes": args.note,
    }
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--target", required=True)
    parser.add_argument("--log", type=pathlib.Path)
    parser.add_argument("--build-log", type=pathlib.Path)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--status", default="NOT_RUN")
    parser.add_argument("--exit-code", type=int)
    parser.add_argument("--architecture")
    parser.add_argument("--execution-environment", default="QEMU")
    parser.add_argument("--framework", default=None)
    parser.add_argument("--framework-version", default=None)
    parser.add_argument("--compiler", default=None)
    parser.add_argument("--compiler-version", default=None)
    parser.add_argument("--qemu-version", default=None)
    parser.add_argument("--configuration-id")
    parser.add_argument("--note", action="append", default=[])
    args = parser.parse_args()
    result = build_result(args)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
