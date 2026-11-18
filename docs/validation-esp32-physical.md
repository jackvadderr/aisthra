# ESP32 Classic Physical Validation

This report documents the first physical hardware validation of Aisthra 0.1.0, covering the C17 engine, ESP-IDF integration, and embedded test suite on an ESP32 DevKit V1.

**Validation scope:** all tests used logical sensors and deterministic observations generated in memory. No physical sensors were connected or used to acquire measurements. Peripheral drivers and measurement accuracy were not validated.

## Test Environment

| Component | Configuration |
|---|---|
| Board | ESP32 DevKit V1 |
| SoC | ESP32-D0WD-V3, revision 3.1 |
| CPU | Dual-core Xtensa LX6, 240 MHz |
| Crystal | 40 MHz |
| Flash | 4 MB |
| Host OS | Gentoo Linux |
| ESP-IDF | 6.1.0 |
| Xtensa GCC | 15.2.0 |
| esptool | 5.3.0 |
| Serial port | `/dev/ttyUSB0` |

The validation project is located at [`tests/embedded/espidf`](../tests/embedded/espidf/). It uses ESP-IDF FreeRTOS and pthread support without requiring PSRAM.

## Firmware and Deployment

| Property | Value |
|---|---|
| Application | `aisthra_esp32_validation.bin` |
| Size | 191,456 bytes |
| SHA-256 | `5d998b01f6b4f75993dcbe195a16a807de706ae467a48d3b999fd9f801fb7e0a` |
| Flash mode | DIO |
| SPI flash frequency | 40 MHz |
| Flash capacity | 4 MB |

The ESP-IDF flashing procedure wrote the following components:

| Offset | Component |
|---|---|
| `0x1000` | Bootloader |
| `0x8000` | Partition table |
| `0x10000` | Application |

All three components reported `Hash of data verified`. No full-chip erase was performed.

### Boot Verification

The serial monitor confirmed:

- ESP-IDF v6.1 bootloader and application startup.
- Correct chip revision and flash configuration.
- Application loaded from `0x10000`.
- Project identified as `aisthra_esp32_validation`.
- Dual-core operation at 240 MHz.
- Normal return from `app_main()` after test completion.

No panic, exception, `Guru Meditation`, or test failure occurred during the complete run.

Repeated reset and partial log lines appeared during the initial monitor connection. Their cause was not established, but the subsequent complete execution succeeded.

## Functional Validation

All required result markers were captured from the physical device:

```text
AISTHRA_CONTRACT:PASS
AISTHRA_EMBEDDED_RESULT target=esp32 status=PASS
AISTHRA_EMBEDDED_SMOKE:PASS
AISTHRA_ESP32_SMOKE:PASS
```

The passing test suite covered:

- Context creation, lifecycle, and cleanup.
- Provider registration, lifecycle, and availability.
- Logical sensor registration.
- Observation validation, publication, and retrieval.
- State snapshots, percepts, and event handling.
- Concurrent operations using pthreads.
- Heap pressure and controlled allocation failure.
- Ten bounded API benchmark workloads.

Both concurrent worker paths completed with `failures=0`.

## Memory and Stack

### Heap Measurements

| Measure | Value |
|---|---:|
| Initial free heap | 289,904 bytes |
| Minimum free heap observed | 195,144 bytes |
| Initial largest free block | 155,648 bytes |
| Largest block during fragmentation probe | 110,592 bytes |
| Free heap after context cleanup | 289,904 bytes |
| Largest free block after cleanup | 155,648 bytes |

Controlled large-allocation and out-of-memory behavior were observed as expected.

The return to initial heap values indicates that the tested lifecycle released its allocated resources. This does not establish the absence of leaks in other workloads.

### Stack Measurements

| Measure | Value |
|---|---:|
| Configured main/pthread stack | 16,384 bytes |
| Main-task high-water mark | 13,056–13,120 bytes |
| Worker-thread high-water mark | 13,464–13,500 bytes |
| Worker failures | 0 |

Stack high-water marks are reported in bytes by the test harness and represent the minimum remaining stack space observed during execution.

These measurements apply to the tested configuration and workload.

## Physical Benchmarks

All ten bounded workloads completed with `status=PASS`.

| Workload | Operations | Elapsed (µs) | Average (µs) | Throughput (op/s) |
|---|---:|---:|---:|---:|
| `context_create_close` | 4 | 969 | 242.250 | 4,127.967 |
| `provider_register` | 8 | 849 | 106.125 | 9,422.850 |
| `sensor_register` | 8 | 1,948 | 243.500 | 4,106.776 |
| `validation` | 32 | 8,865 | 277.031 | 3,609.701 |
| `publication` | 32 | 27,855 | 870.469 | 1,148.806 |
| `latest` | 32 | 2,224 | 69.500 | 14,388.489 |
| `snapshot` | 4 | 1,137 | 284.250 | 3,518.030 |
| `refresh` | 4 | 1,748 | 437.000 | 2,288.330 |
| `percept` | 4 | 29,470 | 7,367.500 | 135.731 |
| `mixed` | 4 | 8,727 | 2,181.750 | 458.348 |

An additional publication workload reported:

```text
count=128 elapsed_us=201273 avg_us=1572.445
```

These results measure synthetic API operations on one physical ESP32. They are not sensor-acquisition latencies or performance rankings between devices.

## Validation Boundaries

### QEMU Results

Previous ESP32 QEMU tests used ESP-IDF 6.1.1, while this physical run used ESP-IDF 6.1.0.

These are independent validation results. No direct numerical QEMU comparison is included because equivalent QEMU runtime logs were unavailable for this physical test.

See [Validation and Testing](validation.md) for the broader platform matrix.

### Remaining Limitations

The physical validation does not establish:

- ADC, GPIO, I²C, SPI, or physical sensor-driver functionality.
- Runtime compatibility with Arduino sketches.
- Compatibility with every ESP32 board variant or external hardware configuration.
- Long-duration stability, power consumption, thermal behavior, or flash endurance.
- Reliability under all application-specific workloads.
- The absence of all memory leaks, race conditions, or concurrency failures.

## Conclusion

**ESP32 CLASSIC — PHYSICAL HARDWARE VALIDATED**

The Aisthra 0.1.0 C17 engine and ESP-IDF integration successfully completed the defined contract, lifecycle, observation-processing, concurrency, memory, and benchmark tests on an ESP32 DevKit V1.

All required validation markers were observed, and no runtime failures occurred during the completed test run.

The result applies to the tested firmware, hardware configuration, and validation workload described in this report.
