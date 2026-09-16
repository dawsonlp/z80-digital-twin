# Examples

**Audience:** users exploring the standalone examples.
**Purpose:** identify what each example demonstrates and how to run it.
**Last reviewed:** 2026-09-15.

## Spectrum source development

Use [the assembly example](../../examples/spectrum-dev/README.md) for VS Code or
terminal build/run, symbols and inspectable Pasmo artifacts. Automated acceptance
uses a separate fixture so edits to the example do not change test expectations.

## GCD Example

Runs a small Z80 program that computes a greatest common divisor.

```bash
./build/gcd_example 1071 462
```

This is a functional correctness example: register use, looping, arithmetic,
and cycle accounting are visible in the program output.

## GCD Stress Test

Runs many cascading GCD calculations and reports throughput.

```bash
./build/gcd_stress_test 10000
```

Use this for rough performance comparison, not CPU semantic validation.

## Performance Benchmark

```bash
./build/performance_benchmark
./build/performance_benchmark --quick
```

For benchmark interpretation, see [Performance](../reference/performance.md).

## Spectrum Probe

`spectrum_probe` is an operational test and diagnosis tool, not just an example:

```bash
./build/spectrum_probe spec48.rom --boot 150 --frames 0 --screen
./build/spectrum_probe spec48.rom --tape underwurlde.tzx --load --screen
```

See [Headless Instrumentation](../testers/headless-instrumentation.md).
