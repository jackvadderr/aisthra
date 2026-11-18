# Validation and Testing

This document records Aisthra's platform validation environments, test coverage, results, and limitations.

## Platform Validation Matrix

| Target | Architecture | Framework | Runtime Validation | Physical Hardware |
|---|---|---|---|---|
| Linux x86-64 | x86-64 | GCC / Clang | PASS — Native | Not applicable |
| ESP32 Classic | Xtensa LX6 | ESP-IDF | PASS — QEMU and physical | PASS — DevKit V1 |
| ESP32-S3 | Xtensa LX7 | ESP-IDF | PASS — QEMU | Not tested |
| ESP32-C3 | RISC-V32 | ESP-IDF | PASS — QEMU | Not tested |
| Cortex-M3 | ARMv7-M | Zephyr | PASS — QEMU | Not tested |
| RISC-V32 | RISC-V 32-bit | Zephyr | PASS — QEMU | Not tested |

**Validation scope:** QEMU results establish runtime functionality within the tested emulator configurations. Physical hardware validation is currently limited to the ESP32 Classic.

All embedded tests used logical sensors and simulated observations generated in memory. No physical sensors or peripheral acquisition drivers were tested.

## Linux Validation

**Toolchains:** GCC 15.3.0 and Clang 22.1.8.

| Test | Result |
|---|---|
| CTest | PASS — 8/8 with each compiler |
| ASan / UBSan | PASS — 7/7 |
| LSan | PASS — 7/7 |
| LibFuzzer | PASS |
| Clang static analysis | No diagnostics |
| Release build and installation | PASS |
| Public ABI | 77 `ais_*` symbols preserved |

Valgrind was unavailable, and full ThreadSanitizer validation was not completed.

## ESP-IDF Validation

Aisthra integrates with ESP-IDF through platform-specific implementations of timing, synchronization, threading, and heap operations.

The following environments were validated:

| Environment | ESP-IDF | Toolchain | Execution |
|---|---|---|---|
| ESP32 Classic | 6.1.1 | ESP GCC 15.2.0 | QEMU 9.2.2 |
| ESP32-S3 | 6.1.1 | ESP GCC 15.2.0 | QEMU 9.2.2 |
| ESP32-C3 | 6.1.1 | ESP GCC 15.2.0 | QEMU 9.2.2 |
| ESP32 Classic | 6.1.0 | Xtensa GCC 15.2.0 | Physical DevKit V1 |

The ESP32 and ESP32-S3 use Xtensa target configurations, while the ESP32-C3 uses the RISC-V toolchain. None of the tested configurations requires PSRAM.

### ESP32 Classic — QEMU vs. Physical Hardware

| Measure | QEMU | Physical DevKit V1 |
|---|---:|---:|
| ESP-IDF version | 6.1.1 | 6.1.0 |
| Firmware size | 162,435 bytes | 191,456 bytes |
| Largest free block before fragmentation | 155,648 bytes | 155,648 bytes |
| Largest free block after fragmentation | 126,976 bytes | 110,592 bytes |
| Stack high-water mark | 13,056 bytes | 13,056–13,120 bytes |
| Functional contracts | PASS | PASS |
| Concurrency | PASS | PASS |
| Benchmarks | PASS | PASS |
| Allocation failure / OOM probe | PASS | Observed as expected |

The physical run also confirmed successful firmware flashing, boot, application execution, and normal completion.

The stack high-water mark represents the minimum remaining stack space observed during execution, not the amount consumed.

Detailed hardware identification, firmware hashes, memory measurements, and benchmarks are available in the [ESP32 Physical Validation Report](validation-esp32-physical.md).

### ESP32-S3 and ESP32-C3 — QEMU

| Measure | ESP32-S3 | ESP32-C3 |
|---|---:|---:|
| Firmware size | 191,989 bytes | 180,380 bytes |
| Largest free block before fragmentation | 327,680 bytes | 172,032 bytes |
| Largest free block after fragmentation | 163,840 bytes | 114,688 bytes |
| Stack high-water mark | 12,756 bytes | 13,372 bytes |

These measurements were collected in QEMU. Physical runtime validation remains pending for both targets.

## Zephyr Validation

**Validated environment:**

- Zephyr 4.5.0
- Zephyr SDK 1.0.1
- QEMU 10.0.2
- Boards: `qemu_cortex_m3` and `qemu_riscv32`

The private platform adapter integrates with Zephyr's mutexes, condition variables, uptime, heap, and stack facilities.

### ARM Cortex-M3

| Measure | Value |
|---|---:|
| Configured RAM | 65,536 bytes |
| RAM used by build | 58,616 bytes (89.44%) |
| Picolibc arena | 40,960 bytes |
| Main stack | 8 KiB |
| Worker stacks | 3 KiB each |

Aisthra does not declare global `_Thread_local` state. The selected libc/toolchain still requires the appropriate Zephyr TLS configuration.

### RISC-V32

| Measure | Value |
|---|---:|
| Picolibc arena | 40,960 bytes |
| Observed allocation | 15,388 bytes |
| Stack high-water mark | 4,536 bytes |

The tested Zephyr `printk` configuration does not support the floating-point formatting required by some benchmark fields. Those values are recorded as `null` rather than estimated.

## Arduino-ESP32 Compatibility

Arduino compatibility was validated through **compilation and linking only**, using Arduino CLI 1.6.0, Arduino-ESP32 3.3.12, and GCC 14.2.0 toolchains.

| Target | FQBN | Build | Runtime |
|---|---|---|---|
| ESP32 | `esp32:esp32:esp32` | PASS | Not tested |
| ESP32-S3 | `esp32:esp32:esp32s3` | PASS | Not tested |
| ESP32-C3 | `esp32:esp32:esp32c3` | PASS | Not tested |

The Aisthra engine is compiled as C17, while Arduino sketches are compiled as C++. The engine does not introduce an Arduino dependency.

The successful physical ESP-IDF validation does not constitute physical Arduino runtime validation.

## Functional and Benchmark Coverage

The embedded validation suite exercises:

- Context creation, destruction, and lifecycle management.
- Provider registration, lifecycle, and availability.
- Sensor registration and observation validation.
- Observation publication and latest-value retrieval.
- State snapshots and percepts.
- Concurrent operations and synchronization.
- Heap pressure, controlled allocation failure, and OOM recovery.
- Ten bounded API benchmark workloads.

### Test Profiles

| Profile | Validation Status |
|---|---|
| `smoke` | PASS — five QEMU targets and physical ESP32 Classic |
| `small` | Not executed in the final matrix |
| `medium` | Not executed in the final matrix |
| `stress` | Not executed in the final matrix |
| `endurance` | Not executed in the final matrix |

The default `smoke` profile includes platform-specific diagnostics:

| Platform | Diagnostics |
|---|---|
| ESP-IDF | ABI widths, heap checkpoints, fragmentation/OOM probe, stack high-water marks, 128-publication metric, and ten benchmarks |
| Zephyr | ABI, arena/OOM, stack, worker concurrency, contracts, and benchmark markers |

Benchmark definitions, result schemas, and profiles are maintained in [`benchmarks/embedded`](../benchmarks/embedded).

**Performance interpretation:** QEMU timing and throughput are configuration-specific diagnostics. They must not be used as direct CPU performance comparisons between architectures or physical devices.

## Validation Limitations

The current validation does not establish:

- Physical sensor acquisition or ESP32 peripheral-driver functionality.
- Physical Arduino runtime compatibility.
- Physical runtime compatibility for ESP32-S3, ESP32-C3, or the tested Zephyr targets.
- Long-duration stability, power consumption, thermal behavior, flash wear, or interrupt latency.
- Complete Valgrind or ThreadSanitizer coverage.

Successful heap recovery and concurrency tests apply only to the workloads executed. They do not establish the absence of all memory leaks, race conditions, or long-term failures.

## Reproducibility

Validation infrastructure and results are organized under:

| Path | Purpose |
|---|---|
| `tooling/embedded/` | Embedded validation scripts |
| `benchmarks/embedded/` | Workloads, profiles, and result schemas |
| `benchmarks/baselines/` | Reference benchmark data |
| `.artifacts/embedded/` | Generated local results |

Results must be regenerated when changes affect the Aisthra engine, platform adapters, build configuration, or test infrastructure.

Physical flashing requires explicit authorization and is not part of the default automated validation workflow.
