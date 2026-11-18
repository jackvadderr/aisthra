#!/usr/bin/env python3
"""Convert one Arduino-ESP32 compile run into a machine-readable result."""

from __future__ import annotations

import argparse
import datetime as dt
import json
import pathlib
import re
from typing import Any


def read(path: pathlib.Path) -> str:
    try:
        return path.read_text(encoding="utf-8", errors="replace")
    except FileNotFoundError:
        return ""


def value_from_build(log: str, pattern: str) -> int | None:
    match = re.search(pattern, log, re.MULTILINE)
    if not match:
        return None
    return int(match.group(1).replace(",", ""))


def marker_value(log: str, marker: str, field: str) -> str | None:
    match = re.search(
        rf"^{re.escape(marker)}:.*?\b{re.escape(field)}=([^\s]+)",
        log,
        re.MULTILINE,
    )
    return match.group(1) if match else None


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--target", required=True)
    parser.add_argument("--architecture", required=True)
    parser.add_argument("--runner-log", type=pathlib.Path, required=True)
    parser.add_argument("--build-log", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--status", required=True)
    parser.add_argument("--exit-code", type=int, required=True)
    args = parser.parse_args()

    runner_log = read(args.runner_log)
    build_log = read(args.build_log)
    build_status = args.status
    source_status = (
        "PASS" if "ARDUINO_SOURCE_COPY: PASS" in runner_log else "NOT_RUN"
    )
    fqbn = marker_value(runner_log, "ARDUINO_BUILD", "fqbn")
    core = marker_value(runner_log, "ARDUINO_BUILD", "core")
    cli = marker_value(runner_log, "ARDUINO_BUILD", "cli")
    artifact = marker_value(runner_log, "ARDUINO_BUILD", "artifact")
    firmware_bytes = value_from_build(
        build_log, r"^Sketch uses\s+([0-9,]+) bytes"
    )
    static_ram_bytes = value_from_build(
        build_log, r"^Global variables use\s+([0-9,]+) bytes"
    )

    notes = [
        "Arduino CLI validation is compile-only; no upload or physical-board execution was performed.",
        "Arduino QEMU execution is not established for this generated Arduino firmware and is reported as NOT_TESTED.",
        "The sketch consumes a temporary copy of the original C17 engine and public headers.",
    ]
    if args.status != "PASS":
        notes.append("Raw runner log: " + str(args.runner_log))
    payload: dict[str, Any] = {
        "schema": "aisthra-arduino-result-v1",
        "generated_at": dt.datetime.now(dt.timezone.utc).isoformat(),
        "target": args.target,
        "architecture": args.architecture,
        "framework": "Arduino-ESP32",
        "framework_version": core,
        "fqbn": fqbn,
        "arduino_cli_version": cli,
        "execution_environment": "Arduino CLI compile",
        "build_status": build_status,
        "qemu_status": "NOT_TESTED",
        "status": build_status,
        "run_exit_code": args.exit_code,
        "source_copy_verification": source_status,
        "firmware_bytes": firmware_bytes,
        "static_ram_bytes": static_ram_bytes,
        "artifact": artifact,
        "logs": {
            "runner": str(args.runner_log),
            "build": str(args.build_log),
        },
        "notes": notes,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    print(f"AISTHRA_ARDUINO_RESULT target={args.target} status={build_status} output={args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
