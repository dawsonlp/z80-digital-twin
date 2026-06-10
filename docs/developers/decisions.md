# Decisions

**Audience:** developers.
**Purpose:** record durable architectural decisions without preserving stale
roadmap text as current work.
**Last reviewed:** 2026-06-09.

## CPU Environment Is Policy-Based

The generic CPU core is templated over memory and I/O policies. Machine-specific
behavior, such as Spectrum ULA ports, stays outside the generic CPU.

Consequence: the fast core path can remain simple, while debugger and machine
configurations add observation or device behavior explicitly.

## I/O Is Device Behavior, Not Port Storage

The default bare-Z80 I/O behavior is open bus. Latching is an opt-in test/simple
device policy, not the default model.

Consequence: Spectrum keyboard, floating bus, EAR, MIC, and border behavior live
in the machine/ULA layer.

## Debugger Runs The Real CPU Configuration

`DebugSession` drives the same CPU configuration used by `SpectrumMachine`.

Consequence: debugger observations are observations of the running machine, not
a proxy.

## Floating Bus Belongs To The ULA

The floating bus is a Spectrum ULA/bus behavior. It is implemented in the
Spectrum ULA path and verified independently by `floating_bus_test`.

Full rationale: [floating-bus-design.md](floating-bus-design.md).

## External Compatibility Assets Stay Local

ROMs, game tapes, and game-derived goldens are not committed. Harnesses must
skip cleanly when those assets are absent.

Consequence: the in-repo test suite stays legally clean and green on a fresh
checkout, while local compatibility runs can still be strict.
