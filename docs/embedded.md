# Embedded Systems

Aisthra uses a single C17 engine implementation in [`src/aisthra.c`](../src/aisthra.c), with platform-specific synchronization and monotonic-clock facilities implemented through private adapters in [`src/internal/aisthra_platform.h`](../src/internal/aisthra_platform.h).

The public API remains consistent across native and embedded platforms, without separate FULL/MICRO variants.

## ESP32 Classic

ESP32 integration uses ESP-IDF facilities for timing, synchronization, threading, and memory management. The validated configurations do not require PSRAM.

### Physical Hardware Validation

Aisthra was successfully validated on an ESP32 DevKit V1 using the project in [`tests/embedded/espidf`](../tests/embedded/espidf/).

| Component | Validated Configuration |
|---|---|
| SoC | ESP32-D0WD-V3, revision 3.1 |
| CPU | Dual-core Xtensa LX6, 240 MHz |
| Crystal | 40 MHz |
| Flash | 4 MB |
| ESP-IDF | 6.1.0 |
| GCC Xtensa | 15.2.0 |
| esptool | 5.3.0 |

The validation passed public API contracts, context and provider lifecycles, observation processing, snapshots, events, pthread concurrency, memory probes, and ten bounded benchmarks.

**Sensor limitation:** all tests used logical sensors and deterministic observations generated in memory. No physical sensors were connected. ADC, GPIO, I²C, SPI, peripheral drivers, and measurement accuracy were not validated.

See the [ESP32 Physical Validation Report](validation-esp32-physical.md) for results, performance measurements, and execution evidence.

### Building and Running

From the repository root:

```bash
cd tests/embedded/espidf

idf set-target esp32
idf build
```

After explicitly authorizing the target device and any replacement of its existing firmware:

```bash
idf -p /dev/ttyUSB0 flash
idf -p /dev/ttyUSB0 monitor
```

The tested Gentoo ESP-IDF package also required `ESP_IDF_VERSION=6.1.0` to be exported in the shell before running `idf`.

The validation harness relies on ESP-IDF FreeRTOS and pthread support. It is maintained under `tests/embedded/` rather than `examples/`, which contains introductory C and Arduino applications.

## Arduino-ESP32

Arduino sketches use C++ to access Aisthra through its C-compatible public headers. The engine remains compiled as C17 and does not depend on `Arduino.h`.

Compilation and linking passed for ESP32, ESP32-S3, and ESP32-C3 using Arduino-ESP32.

**Validation scope:** build compatibility only. Physical Arduino runtime execution has not been tested.

## Other Embedded Targets

The following platforms passed functional contracts, concurrency, memory, and benchmark tests under QEMU:

- ESP32-S3 — Xtensa LX7, ESP-IDF.
- ESP32-C3 — RISC-V32, ESP-IDF.
- ARM Cortex-M3 — Zephyr.
- RISC-V32 — Zephyr.

These results establish emulator-level runtime validation, not physical hardware compatibility.

## Portability and Limitations

The Aisthra engine does not define global `_Thread_local` state, although some platform libraries may require their own thread-local storage configuration.

QEMU timing results are configuration-specific diagnostics and should not be used to compare physical CPU performance.

Physical power consumption, thermal behavior, flash wear, interrupt latency, and long-duration endurance remain unvalidated.

For the complete platform matrix, toolchain versions, memory measurements, benchmark results, and outstanding validation coverage, see [Validation and Testing](validation.md).
