# Performance

**Audience:** users and developers interpreting benchmark results.
**Purpose:** explain how to measure performance without relying on stale numbers.
**Last reviewed:** 2026-06-09.

## Benchmark Command

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DZ80_BUILD_UI=OFF
cmake --build build -j
./build/performance_benchmark
./build/performance_benchmark --quick
```

Use Release builds for performance numbers. Debug builds are for diagnosis.

## Interpretation

Performance depends on compiler, CPU, build flags, and selected CPU environment
policy. The fastest path uses the bare CPU configuration; debugger and Spectrum
machine configurations intentionally add observation and device behavior.

Report benchmark results with:

- commit hash;
- compiler and version;
- build type and CMake options;
- host CPU/OS;
- exact command.

Historical analysis is kept in
[../archive/performance-analysis.md](../archive/performance-analysis.md), but
current claims should be regenerated from the benchmark binary.
