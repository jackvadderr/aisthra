# Development and Installation

## Requirements and Build

- CMake 3.20 or newer.
- A C17 compiler.
- Suitable threading support for the selected platform.

```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo -DAISTHRA_BUILD_TESTS=ON
cmake --build build
```

For a Release library without tests:

```bash
cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release \
  -DAISTHRA_BUILD_TESTS=OFF
cmake --build build/release
```

## Installation

```bash
cmake -S . -B build/package -DCMAKE_BUILD_TYPE=Release \
  -DAISTHRA_BUILD_TESTS=OFF
cmake --build build/package
cmake --install build/package --prefix "$PWD/.artifacts/install"
```

An installed consumer uses:

```cmake
cmake_minimum_required(VERSION 3.20)
project(example LANGUAGES C)
find_package(aisthra CONFIG REQUIRED)
add_executable(example main.c)
target_link_libraries(example PRIVATE aisthra::shared)
```

Use `aisthra::static` for a static link. Installation contains headers,
libraries, CMake package metadata, and `LICENSE`.

## Testing

```bash
cmake -S . -B build/test -DAISTHRA_BUILD_TESTS=ON
cmake --build build/test
ctest --test-dir build/test --output-on-failure
```

Available runners:

```bash
tests/runners/run-all.sh
tests/runners/run-sanitizers.sh
tests/runners/run-static-analysis.sh
tests/runners/run-fuzz.sh
tests/runners/run-valgrind.sh
tests/runners/run-valgrind-tools.sh
benchmarks/runners/run-benchmarks.sh
benchmarks/runners/run-stress.sh
```

ASan, UBSan, LSan, scan-build, and LibFuzzer are supported when available.
Valgrind, perf, and a fully functioning ThreadSanitizer runtime are
environment-dependent and must be reported as unavailable or limited.

## Benchmarking

```bash
AISTHRA_BENCHMARK_BUILD_ROOT=.artifacts/bench-build \
AISTHRA_BENCHMARK_OUTPUT=.artifacts/bench.jsonl \
benchmarks/runners/run-benchmarks.sh
```

Benchmark JSONL contains nanoseconds per operation, throughput, CPU time, peak
RSS, accepted/rejected counts, and a checksum. Do not compare instrumented
builds with native Release measurements.

## Compatibility

The validated matrix covers Linux x86-64 with GCC and Clang, C17 consumers,
C++17 header consumers, static linking, shared linking, and CMake installation.
Other systems and architectures require a conforming C17 compiler and C11
threads implementation but are not certified by this source release.

## Versioning

The candidate version is ****0.1.0****. The library version is `0.1.0`, API and
ABI versions are `1.0`, schema version is `1.0`, and the build ID is
`aisthra-c17-0.1.0`.

Patch releases preserve ABI and structure contracts. Minor releases may add
compatible symbols, enum values, schemas, and optional fields. Incompatible
layout, ownership, lifecycle, synchronization, or symbol changes require a
major release.