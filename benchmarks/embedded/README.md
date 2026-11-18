# Embedded benchmark profiles

The A.13 embedded runner emits one JSON result per target using
[`result.schema.json`](result.schema.json). The public benchmark covers bounded
context, provider, sensor, validation, publication, latest-value, snapshot,
refresh, percept-state, and mixed workloads. It runs after the contract suite
and reports measured QEMU time, operation count, latency, and throughput.

The profile names are deliberately explicit:

| Profile | Purpose | Default A.13 state |
| --- | --- | --- |
| `smoke` | Boot, public contract, bounded workload set, heap/stack probes | Executed by QEMU runners |
| `small` | Short repeated workload set | `NOT_RUN` unless explicitly scheduled |
| `medium` | Representative repeated workload set | `NOT_RUN` unless explicitly scheduled |
| `stress` | High publication and snapshot pressure | `NOT_RUN` unless explicitly scheduled |
| `endurance` | Long-duration stability run | `NOT_RUN` unless explicitly scheduled |

Missing measurements are represented as JSON `null`; they are never replaced
with estimates. QEMU latency and throughput are diagnostic measurements for a
specific emulated machine and configuration. They are not cross-target CPU
benchmarks and do not certify physical-board timing, power, flash endurance, or
thermal behavior.

Run the complete target matrix with:

```bash
tooling/embedded/run-matrix.sh
```

Results and raw logs are written below `.artifacts/embedded`, which is ignored
by the source tree. No runner flashes hardware.

## Arduino-ESP32 compile results

The Arduino consumer is intentionally separate from the engine. The sketch in
`examples/arduino/AisthraSmoke` is compiled against a temporary copy of the
original `src/aisthra.c`, private platform adapter, and public headers. The
runner verifies the copies before invoking Arduino CLI, so a successful result
is a consumer/build validation rather than a second implementation of Aisthra.

Run the three official Arduino-ESP32 board definitions with an isolated local
CLI configuration:

```bash
AISTHRA_ARDUINO_CLI=/path/to/arduino-cli \
AISTHRA_ARDUINO_INSTALL_CORE=1 \
tooling/embedded/run-arduino-matrix.sh
```

The result is written to `.artifacts/arduino/results/matrix.json` and follows
[`arduino-result.schema.json`](arduino-result.schema.json). The build status is
`BUILD_VALIDATED` only when all three sketches compile; Arduino QEMU execution
is always recorded as `NOT_TESTED` until a supported Arduino-to-QEMU route is
established. The checked-in compile baseline is
[`arduino-build.jsonl`](../baselines/arduino-build.jsonl). The runner
never uploads or flashes a board.
