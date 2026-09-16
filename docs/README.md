# Documentation

**Audience:** users, developers and testers.
**Purpose:** route readers to the smallest current document answering their question.
**Last reviewed:** 2026-09-15.

Start with [current status](reference/status.md) for implemented capability and
limits. [Roadmap](developers/roadmap.md) gives current sequencing. Dated plans,
checklists and verification records describe their own scope and are not blanket
claims of current completion.

## Use the tools

- [Getting started](users/getting-started.md): prerequisites, build and main tools.
- [Spectrum assembly development](../examples/spectrum-dev/README.md): VS Code
  or terminal, external Pasmo, build artifacts and a fresh debugger launch.
- [Debugger](users/debugger.md): execution, address/history browsing, observation
  markers, symbols, clear-analysis and restart behavior.
- [Spectrum viewer](users/spectrum-viewer.md): keyboard, tape, screen and beeper.
- [Examples](users/examples.md) and [troubleshooting](users/troubleshooting.md).
- [Language server](../lsp-z80/README.md) and
  [VS Code client](../lsp-z80/editors/vscode/README.md).

## Understand and extend the implementation

- [Architecture](developers/architecture.md): actual targets, policies and ownership.
- [Debugger design](developers/debugger-design.md): execution, evidence and lifecycle.
- [Spectrum design](developers/spectrum-machine-design.md): runtime/devices and fidelity limits.
- [Floating bus](developers/floating-bus-design.md): detailed timing rationale.
- [Decisions](developers/decisions.md) and [contributing](developers/contributing.md).
- [Current handoff](../handoff.md): short continuation guide.

## Plan and acceptance records

- [Current roadmap](developers/roadmap.md): delivered foundations and remaining priorities.
- [Address metadata](developers/address-metadata-checklist.md): partial implementation
  acceptance, including unresolved audit/resource/native checks.
- [ROM-first reverse loop](developers/rom-reverse-loop-checklist.md): path to durable
  evidence, annotations and range export; not a completed product loop.
- [Enhanced roadmap](developers/enhanced-roadmap.md) and
  [reverse-engineering roadmap](developers/reverse-engineering-roadmap.md): longer-term proposals.
- [Forward-loop plan](developers/spectrum-development-loop-plan.md) and
  [observed-browsing checklist](developers/observed-disassembly-checklist.md): dated delivery records.
- [July architecture review](developers/source-architecture-feedback.md): historical
  findings, partly addressed by subsequent runtime consolidation.

## Verify behavior

- [Testing](testers/testing.md): CTest, dependencies/skips, LSP and extension checks.
- [Current refresh verification](testers/documentation-refresh-verification.md):
  fresh results and explicit limitations.
- [Forward-loop verification](testers/spectrum-development-loop-verification.md):
  historical terminal/editor/native acceptance evidence.
- [Disassembly verification](testers/disassembly-verification.md): independent
  encoding fixtures and byte-exact export/reassembly contract.
- [CPU suites](testers/cpu-correctness-suites.md),
  [headless instrumentation](testers/headless-instrumentation.md),
  [compatibility plan](testers/compatibility-plan.md),
  [stabilization plan](testers/stabilization-harness-plan.md),
  [fault diagnosis](testers/fault-diagnosis.md), and [test assets](testers/test-assets.md).

## Reference and history

- [Status](reference/status.md) and [performance measurement](reference/performance.md).
- [Changelog](../CHANGELOG.md): released changes and unreleased development.
- [Archive](archive/README.md): superseded designs and handoffs, retained as history.
