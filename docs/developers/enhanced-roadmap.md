# Z80 Digital Twin — Enhanced Roadmap

*Assembly development, live execution, and progressive reverse engineering*

**Requirements draft · 5 September 2026 · Version 1.0**

**Repository baseline:** `dawsonlp/z80-digital-twin`, `main`, commit `928ef0328377d3dce346e38f3d1840894cd00b97` (11 June 2026). Inspected from a fresh GitHub checkout on 5 September 2026.

**Status:** Proposed requirements and sequencing, submitted for review. Existing capabilities are identified separately below. This document does not approve an architecture, select an assembler, or claim that the proposed workflow is implemented.

**Integration update · 7 September 2026:** The project owner selected Pasmo as the first assembler/dialect and moved `lsp-z80` into this repository as a top-level subproject while retaining its independent package and process boundary. Baseline statements below still describe the pinned pre-integration commit unless explicitly marked as proposed.

**Contents:** [Purpose](#1-purpose) · [Scope](#2-scope-and-authority) · [Repository baseline](#3-verified-repository-baseline) · [Constraints](#4-governing-constraints-and-evidence-rules) · [Requirements](#5-functional-requirements) · [Milestones](#6-delivery-sequence-and-relationship-to-the-existing-roadmap) · [Acceptance](#7-end-to-end-acceptance-scenarios) · [Decisions](#8-product-decisions-deferred-decisions-and-open-questions) · [Review](#9-review-and-sign-off) · [Sources](#10-source-register)

## 1. Purpose

Extend Z80 Digital Twin into a workbench in which a developer can write Z80 assembly in VS Code, obtain semantic editing assistance through an LSP, assemble the program, deploy it into an already running emulator, and execute and inspect it. The same workbench must support starting from a binary or running machine, accumulating execution evidence, refining names and explanations, and exporting documented assembly that can be rebuilt and checked against a captured memory image.

The intended user workflow is:

**Edit → assemble → deploy → execute → observe → refine → save → export → reassemble → verify.**

The durable result is the user's accumulated understanding together with the evidence and bytes it describes. A plausible disassembly, a successful build, and a byte-exact reconstruction establish different things; the product must report each separately.

### Success outcomes

1. **Develop:** A multi-file assembly project has dependable diagnostics, navigation, completion, symbols, and safe renaming in VS Code.
2. **Run:** A specific successful build can be loaded into the selected running emulator with explicit placement and start state, then executed without restarting the emulator process.
3. **Understand:** A user can explore unfamiliar code, inspect counts and execution flow, recognize writes that become executable code, and progressively refine persistent symbols and annotations.
4. **Reconstruct:** A selected capture can become documented source whose assembled bytes are compared against that exact capture, with mismatches and limitations made visible.

## 2. Scope and authority

### Governing inputs

- The user's request for the four capabilities above, including frame-loop detection, execution counts, symbol refinement, and self-modifying-code detection.
- The inspected repository's current layering, optional instrumentation, headless verification, and generic CPU performance constraints, treated as compatibility requirements for this extension. [R3, R4]

### Inputs consulted and supporting context

- Current development roadmap, last reviewed 10 June 2026. [R1]
- Reverse-engineering laboratory roadmap, including its L1–L9 capabilities. [R2]
- Architecture, build definitions, current status, debugger execution, memory observation, disassembler, symbols, UI loading/frame control, machine timing, and relevant tests. [R3–R13]
- The supplied preview of “Writing A Z80 LSP Server.” It supplies intended use cases, not evidence of implementation. Illustrative rates, labels, and confidence values in that conversation are not measured facts.
- Official LSP and VS Code documentation for interoperability boundaries. [P1–P3]

### In scope

- Generic Z80 assembly editing and a first complete assembler-dialect profile.
- Local desktop VS Code integration and control of a locally running Z80 Digital Twin emulator/debugger instance.
- The existing generic Z80 execution environment and ZX Spectrum 48K machine, with the latter as the first machine-specific acceptance target.
- Raw binary insertion, assembler output and symbol import, controlled execution, live and captured disassembly, execution evidence, persistent annotations, source export, and round-trip verification.
- Headless equivalents for build, deployment, capture, export, and verification so the workflow can be reproduced without the GUI.

### Deferred scope

Sinclair ZX Spectrum BASIC remains a future language target from the conversation; a BASIC LSP, tokenized BASIC editing, and BASIC-to-machine-code semantic linking are not part of this assembly release. Spectrum 128K banking, arbitrary additional machines, physical hardware deployment, remote multi-user editing, reverse execution/time travel, and automatic recovery of original source or programmer intent are also deferred. Additional dialects and standard snapshot interchange follow a proven first path.

## 3. Verified repository baseline

“Present” below means inspected source exists; the validation paragraph states which behavior was actually tested. “Not found” is limited to the inspected commit and searched source/build/documentation tree.

| Area | Current evidence | Requirement consequence |
|---|---|---|
| CPU and build | C++23 `z80_cpu`; compile-time memory/I/O policies; optional GUI build. [R3, R4] | Preserve the fast generic CPU configuration and keep development tooling optional. |
| Capability boundaries | `z80_debugger_core` contains execution, disassembly, and symbols; `z80_machine` is an interface target; UI composition is separate. [R4] | New editing and analysis behavior must remain usable headlessly and respect existing dependency boundaries. |
| Debugger | Concrete `DebugSession` over `DebugCPU`, with step, step-over, run, T-state budgets, breakpoints, write watchpoints, and stop reasons. [R5] | Reuse the behavior; add reliable external session control and state coordination. |
| Disassembly | Shared decoder returns bytes, length, text, substituted symbols, and a static direct-branch target. [R7] | This is a useful decoder, not an assembly-source parser or an observed control-flow graph. |
| Symbols | Address/name lookup, typed symbols, descriptions, region size, JSON `.sym` load/save, and UI symbol editing. [R8] | Extend durable annotation and identity handling; basic naming is already available. |
| Coverage | Opcode-start and operand flags, distinct covered-byte total, and coverage percentage. `RecordCoverage` returns early for a previously seen start. [R5] | Execution counts, time windows, code-version history, and reclassification after changed instruction lengths are additional requirements. |
| SMC | Writes to previously executed opcode/operand bytes record address, old/new value, writer PC, and cycle count; break-on-SMC exists. Event storage is capped at 8,192 while the total continues. Reset clears coverage and event history. [R5] | Add write-before-first-execution correlation, temporal instruction versions, retention visibility, and persistence. Current SMC counts are code-write counts and can include unchanged-value writes. |
| Memory observation | Committed-write and blocked-write hooks; reads are not observed; `RawWrite` bypasses both observers and protection. [R6] | Do not claim dynamic read references or complete load provenance from existing hooks. Distinguish CPU writes, host loads, manual edits, and blocked attempts. |
| Loading | `LoadProgramFile` resets the CPU and calls `LoadProgram`; the CPU loader copies bytes only while addresses remain within 64 KB. CLI binary loading and symbol load/save exist. [R9, R10] | Current loading is not a transactional build/deploy/execute workflow. Overflow must not become silent truncation in the new workflow. |
| Spectrum timing | PAL timing constants and machine frame scheduling exist. The debugger frontend has its own `DriveSpectrumFrame` path, preserving an open frame at breakpoint/watchpoint/SMC stops. [R9, R11] | Frame evidence must describe emulated frames across all supported execution paths, not UI redraws or a count of resume operations. |
| Editing/build integration | No assembly LSP, VS Code extension manifest, configured assembler workbench, or complete source-export/round-trip implementation was found. Their foundations appear in the roadmaps. [R1, R2, R4] | These are new deliverables, not configuration of an already implemented end-to-end feature. |

### Documentation reconciliation

The architecture document describes a templated debugger/configuration approach, while the current execution header defines a concrete `DebugSession` and a `DebugCPU` using `ObservableIo<CallbackIo>`. The build also makes `z80_machine` an interface target, despite the architecture's broad static-library wording. Preserve the intended boundaries, but use actual code when planning integration; this roadmap does not require a template conversion. [R3–R5]

The reverse-engineering roadmap marks L1/L2 complete, yet some later “near-term seeds” still describe adding their hooks. Its optional execution counts are not present in the inspected coverage implementation. Its inexpensive/“exact” observation claims must not be carried forward as proven performance or complete historical semantics. [R2, R5]

### Validation performed for this document

A fresh checkout was configured in Release mode with GUI dependencies disabled, using AppleClang 21.0.0. The existing `observable_memory_test`, `machine_test`, `debug_session_test`, `disassembler_test`, and `symbol_table_test` were built and run: **5/5 passed**. These focused tests substantiate the relevant foundation. No full CPU compatibility suite, ROM/game run, GUI interaction, LSP session, or proposed workflow was validated. External-asset-dependent status claims remain repository-reported. [R12]

## 4. Governing constraints and evidence rules

All requirements below are proposed obligations. **Must** means required for the milestone that contains the requirement, rather than a claim about current behavior. M0–M6 are defined in Section 6.

| ID | Requirement | Acceptance evidence |
|---|---|---|
| GOV-01 | Preserve the existing separation between generic CPU, UI-free capabilities, and frontends. Optional development/analysis features must not impose work on the bare fast configuration. | Headless build and dependency review succeed; existing correctness checks and an agreed bare-CPU benchmark comparison pass. |
| GOV-02 | Distinguish observed execution, static inference, user assertion, imported knowledge, and unknown information wherever they affect interpretation. | A declared data region that later executes retains both the declaration and contradictory execution evidence, visibly identified. |
| GOV-03 | Associate observations and annotations with the relevant artifact, address context, session, and memory version or capture. An address alone must not imply continuity across replacement programs. | Loading different bytes at the same address does not silently attach old evidence or confirmed meanings to the new program. |
| GOV-04 | Keep editing, build, deployment, execution, and annotation actions distinguishable. Merely opening, analyzing, hovering over, or renaming a document must not execute code or change emulator bytes. | Read-only editing operations leave captured machine state unchanged; machine mutations have explicit user actions and outcomes. |
| GOV-05 | Preserve incomplete evidence honestly. Unexecuted does not mean data or unreachable; high execution frequency does not establish purpose; byte equivalence does not establish semantic correctness. | Listings, proposals, and verification reports use distinct claims and retain unresolved regions. |
| GOV-06 | Use timing and instruction semantics appropriate to the selected target. Identify unsupported dialect forms and known machine-fidelity limits. | Reports identify target/dialect; Spectrum timing claims disclose missing contention where relevant. |
| GOV-07 | Keep new workflows reproducible with redistributable synthetic fixtures; external ROM/game assets remain optional and locally supplied. | Core acceptance runs without copyrighted assets; missing optional assets are reported as skipped, not tested. |
| GOV-08 | Preserve existing `.sym` knowledge and user-approved annotations through migration, reanalysis, and reload. | Legacy files import with explicit warnings for unsupported content; suggestions and build imports do not silently overwrite user refinements. |

## 5. Functional requirements

### 5.1 Z80 assembly language server — M1

The LSP supplies editing semantics. It must remain useful with the emulator stopped or absent. Standard language features and lifecycle behavior follow the selected LSP compatibility baseline; machine control is a separate user capability. [P1, P2]

| ID | Requirement | Acceptance evidence |
|---|---|---|
| LSP-01 | Provide an independently usable LSP server with capability negotiation, document synchronization, orderly startup/shutdown, cancellation, and isolated diagnostic logging. Advertise only supported operations. | A protocol test client initializes, edits, queries, cancels work, and shuts down without protocol corruption or orphaned work. |
| LSP-02 | Apply one explicitly configured assembler-dialect profile consistently to instructions, operand forms, labels/scopes, expressions, directives, includes, macros, and conditional assembly supported by that profile. | The first dialect corpus is handled consistently with its assembler; absent/unsupported configuration produces a clear limitation rather than silent dialect guessing. |
| LSP-03 | Analyze multi-file projects, include paths, configured definitions, and unsaved document versions. Invalidate results when a dependency or build configuration changes. | An include edit updates dependent diagnostics and navigation; older results do not replace results for newer document versions. |
| LSP-04 | Report syntax and semantic problems with useful source locations: invalid forms, unresolved/duplicate symbols, include failures, invalid expressions, and computable range errors. Distinguish incomplete analysis from confirmed errors. | Positive and negative fixtures produce expected diagnostics, including macro/include origin information when available. |
| LSP-05 | Offer context-sensitive completion for instructions, operands, directives, and in-scope symbols, plus hover for symbol meaning and known instruction effects, size, flags, and timing. Conditional timing and machine-dependent costs must be qualified. | Completion respects the configured dialect/scope; hover distinguishes resolved values from estimates or unavailable information. |
| LSP-06 | Provide definitions, references, document/workspace symbols, and semantic highlighting with correct scopes. References must come from interpreted uses rather than matching every occurrence of a name. | Local labels, names reused in different scopes, comments, strings, macros, and includes do not produce incorrect navigation. |
| LSP-07 | Provide conservative source-symbol rename through client-applied, version-aware edits, with preview and refusal when scope or expansion semantics cannot be established safely. | Renaming changes the intended definition and references only; collisions, stale versions, and ambiguous macro-generated names are rejected or clearly bounded. |
| LSP-08 | Keep static source facts distinct from runtime facts. Display a source address or runtime count only when a matching build/session mapping is available. | Editing after deployment marks affected runtime associations stale; static editing continues without a connected emulator. |

### 5.2 VS Code development experience — M1–M3

VS Code language support and debugger integration are different extension capabilities. The exact packaging and use of a Debug Adapter Protocol adapter are design decisions, not preconditions silently imposed by this roadmap. [P2, P3]

| ID | Requirement | Acceptance evidence |
|---|---|---|
| VSC-01 | Provide an installable VS Code experience with assembly file association, syntax support, LSP lifecycle management, configuration help, and useful startup-failure messages. | A fresh supported VS Code installation can open a supplied sample and obtain working language features from documented setup steps. |
| VSC-02 | Expose per-project dialect, assembler location/arguments, include paths, build entry, output selection, load placement, and emulator selection. Keep unrelated workspace folders independent. | Two projects with different configurations do not exchange symbols, output files, diagnostics, or deployment targets. |
| VSC-03 | Expose clearly named Build, Deploy, Run, Pause, Step, and Build-and-Run actions, with visible current artifact, target, connection, and execution state. | A user can complete the workflow without guessing which binary or instance will run; failed prerequisite actions stop the sequence. |
| VSC-04 | Present assembler diagnostics as navigable Problems entries and retain build output for diagnosis. Distinguish assembler messages from LSP diagnostics. | A deliberate error opens the correct file/line and cannot be mistaken for successful deployment. |
| VSC-05 | Allow users to inspect PC/registers, memory, disassembly, and stop reasons, set/remove instruction breakpoints, and run/pause/step the attached target. | A selected instruction breakpoint stops the emulator and all displayed state corresponds to the same stop. |
| VSC-06 | Map source breakpoints and execution position through the deployed build's actual address mapping. Fall back to address/disassembly when source mapping is unavailable or ambiguous. | Multi-instruction source lines, included files, macro expansions, and stale source show resolved locations or an explicit unresolved state. |
| VSC-07 | Expose symbols, annotations, counts, frame evidence, SMC events, and cross-references in navigable views as their milestones land. Generated listing positions must retain their address/capture meaning through refreshes. | Renaming a symbol or expanding a listing does not move a breakpoint to unrelated bytes or discard the user's current selection. |
| VSC-08 | Recover visibly from server/emulator disconnection, support cancellation of long work, and honor workspace execution trust. Opening a project must not automatically invoke its assembler or mutate a target. | Disconnect/reconnect and cancellation tests preserve edits and identify stale machine state; untrusted project opening does not run configured commands. |

### 5.3 Assemble, build, and identify artifacts — M2

| ID | Requirement | Acceptance evidence |
|---|---|---|
| BLD-01 | Invoke a configured external assembler with a defined working directory, inputs, arguments, environment requirements, and supported version/dialect. The first supported toolchain must be documented and reproducible. | A sample builds from VS Code and headlessly with the same effective configuration and output bytes. |
| BLD-02 | Bind every build result to its source revision or captured source content, configuration, assembler identity, exit result, and output hash. Make the unsaved-source build policy visible. | The user can determine which source was assembled; an edited buffer cannot masquerade as the deployed source revision. |
| BLD-03 | Treat missing tools, timeout/cancellation, nonzero exit, missing output, malformed output, and unexpected empty output as distinct failures. Never fall back silently to a previous binary. | Each failure fixture leaves deployment unstarted and names the failure; a stale file left by an earlier build is not accepted as fresh output. |
| BLD-04 | Identify output ranges, origins, lengths, entry point information, and symbol/source maps where the toolchain supplies them. Require explicit placement when raw bytes contain none. | Multiple outputs or conflicting origin/load information require a resolved selection; unsupported layouts are rejected before loading. |
| BLD-05 | Import assembler symbols and source mappings with build provenance, distinguish them from inferred/user names, and surface conflicts with existing knowledge. | Rebuilding with moved labels updates matching build symbols while preserving user annotations or flagging associations for review. |
| BLD-06 | Make build artifacts and diagnostics available without an emulator and permit reuse of a selected successful artifact for deployment and round-trip verification. | The same identified binary can be deployed repeatedly; offline build succeeds without an active session. |

### 5.4 Live deployment and execution — M2–M3

“Deploy into the running emulator” means using the existing emulator instance. Execution may be paused at a defined instruction boundary while state changes are applied; uninterrupted execution during patching is not required.

| ID | Requirement | Acceptance evidence |
|---|---|---|
| RUN-01 | Select and identify the intended live instance, machine model, capabilities, and state before deployment. Detect stale/disconnected/incompatible sessions. | With two instances open, only the selected compatible target changes; reconnect does not silently retarget a pending operation. |
| RUN-02 | Validate the complete proposed load before changing the machine: address-space bounds, overlap, writable regions, output length, and conflicts with the chosen memory layout. ROM modification requires an explicit nonstandard mode. | An out-of-range, protected, overlapping, or ambiguous load fails with no partial memory/register change; no silent 64 KB wrap or truncation occurs. |
| RUN-03 | Apply deployment at a safe stopped boundary and report success only after all selected bytes and start-state changes are installed and verified. Prevent execution of a partially installed artifact. | A failure leaves the prior state intact, or restores it; failed recovery leaves the target paused and explicitly unusable until repaired. |
| RUN-04 | Let the user choose preservation versus reset and specify PC, SP, applicable registers, interrupt state, and whether to remain paused or run. Show defaults and changed ranges before execution. | A preserve-state deployment changes only its declared state; a reset deployment performs its documented reset; setting PC alone is not treated as a complete machine reset. |
| RUN-05 | Bind deployed bytes, symbols, source mappings, and loaded artifact identity together. Record load provenance and invalidate conflicting prior analysis. | Inspection identifies the exact deployed build/hash/ranges; a new program at an old address does not inherit stale coverage as current evidence. |
| RUN-06 | Support raw binary insertion independently of assembling, with explicit placement, optional CPU-state preset, source identity, and verification of resulting bytes. | A binary-only fixture loads into the selected RAM range and can be run and disassembled with no source or symbol file. |
| RUN-07 | Distinguish deployment writes, manual debugger edits, reset/restore operations, and emulated CPU writes. Host operations must not be attributed to the last executing instruction. | Reloading a previously executed range creates a deployment record rather than false CPU-authored SMC events. |
| RUN-08 | Coordinate execution control between VS Code, the debugger UI, and headless actions. Serialize conflicting mutations and present truthful stop reasons; preserve normal Spectrum HALT/interrupt behavior. | Concurrent run/load requests resolve predictably; a paused mid-frame program resumes without generating an extra frame interrupt or losing its stop context. |

### 5.5 Live disassembly and progressive semantic refinement — M3–M4

| ID | Requirement | Acceptance evidence |
|---|---|---|
| SEM-01 | Offer both live disassembly and explicitly frozen captures, including selected ranges, bytes/hashes, CPU context, machine configuration, and observation boundary. | A capture taken during execution is internally consistent; later mutation changes the live view while the capture remains reproducible. |
| SEM-02 | Distinguish observed instruction starts/spans, statically proposed code, user-declared code/data, inferred data, and unknown bytes. Preserve overlapping interpretations and temporal conflicts. | Mixed code/data, a branch into an apparent operand, and later reuse of code as data remain visible without forced single classification. |
| SEM-03 | Anchor decoding to known or declared starts and make speculative decoding explicit. Show unsupported or undecodable encodings as recoverable bytes. | Scrolling backward or across an unknown region does not promote guessed boundaries into observed facts. |
| SEM-04 | Support synthetic labels followed by user refinement into names, descriptions, routine roles, comments, constants, and typed data regions, including byte/word/string/pointer-table declarations. | A user can change `SUB_8140` to a chosen name, describe its role, mark associated data, and retain that work after stepping and reopening. |
| SEM-05 | Preserve semantic identity within its valid artifact/context when a symbol name changes, and update its listing, references, views, and later exports. Keep previous names/history recoverable. | A rename updates all relevant views while preserving evidence and cross-references; unrelated code at the same address in another context is unaffected. |
| SEM-06 | Allow users to accept, amend, reject, or undo inferred labels and roles. Reanalysis must preserve decisions and surface contradictory new evidence. | Rejecting a routine proposal or refining its boundaries survives reanalysis; new contradictory behavior appears for review rather than silently changing a confirmed meaning. |
| SEM-07 | Distinguish annotation rename from source rename and byte patching. Generated listings must make editable annotation content and byte-changing source edits clear. | Renaming a recovered label changes presentation/exported names, not machine bytes; changed assembly requires the explicit build/deploy workflow. |
| SEM-08 | Provide static and observed cross-references and proposed routines/basic blocks, with uncertainty for indirect transfers, unusual stack use, tail calls, and overlapping code. Do not invent dynamic read evidence from static operands. | A computed jump gains an observed target only when seen; unresolved transfers and unobserved memory accesses remain explicitly incomplete. |

### 5.6 Execution evidence, frequency, and frame-loop detection — M4

| ID | Requirement | Acceptance evidence |
|---|---|---|
| EVT-01 | Count observed instruction executions separately from unique covered addresses/bytes. Define treatment of prefixes, repeating block instructions, interrupts, and HALT idle time. | A deterministic trace gives exact expected counts; repeated prefixes are not accidentally counted as independent complete instructions, and idle cycles are not fabricated instruction executions. |
| EVT-02 | Provide cumulative and selected-window counts, first/last observation, and rates with explicit denominators: emulated time, observed frames, or wall time. Keep counts associated with applicable code versions. | A known loop reports correct counts/rates in real-time, turbo, pause/resume, and single-step operation; incompatible time bases are not mixed. |
| EVT-03 | Record actual control-flow outcomes, including taken/fallthrough branches, calls, returns, restart/interrupt transfers, and dynamically resolved targets where observed. | Conditional and indirect-control-flow fixtures distinguish possible edges from edges actually taken, with counts and observation intervals. |
| EVT-04 | Correlate execution with emulated video-frame identity and position for Spectrum mode. Distinguish frame boundaries, interrupt assertion, and interrupt service. Partial frames and missing observations must be identified. | A breakpoint halfway through a frame and subsequent resume do not double-count a frame; masked interrupts do not appear as serviced handlers. |
| EVT-05 | Propose frame-correlated loop/routine roles from repeated control flow and measured per-frame behavior. Show the observation window, frequency distribution, supporting calls/edges, and conflicting evidence. | A synthetic outer loop, inner copy loop, and frame interrupt routine receive distinguishable evidence; none is automatically asserted to be the game loop solely because it occurs once per frame. |
| EVT-06 | Allow the user to name and confirm a candidate as `game_loop` or another role, and refine that interpretation during execution. Support multiple candidates, missed frames, variable work, and multi-frame updates. | The user confirms a candidate, then changes it after a second execution phase; historical evidence and the revision remain accessible. |
| EVT-07 | Surface hotspots, unvisited regions, counts, and frame relationships through navigable listings/hover/views without presenting partial exploration as whole-program coverage. | Reports identify selected range and observation coverage; an unvisited alternative path remains unknown rather than declared dead. |
| EVT-08 | Make collection modes, retention bounds, dropped events, counter overflow behavior, and time precision explicit. Distinguish exact aggregates from sampled or truncated traces. | A deliberately exceeded event limit produces a visible gap/retention indication; historical reconstruction and loop confidence do not claim evidence that was discarded. |

### 5.7 Self-modifying and newly generated executable code — M4

Three separate observations matter: **a write to previously executed bytes**, **a written byte later consumed in an instruction**, and **execution of a changed instruction version**. They may occur together, but one must not stand in for another. Data copied by a loader and later executed is evidence of loaded/generated executable code; its classification as self-modification needs context.

| ID | Requirement | Acceptance evidence |
|---|---|---|
| SMC-01 | Retain existing detection of writes to previously executed opcode or operand bytes, and separately correlate writes with later instruction consumption even when that address had never executed before the write. | Both a patched known instruction and a freshly unpacked routine are detected, with the applicable evidence category stated. |
| SMC-02 | Link a committed CPU write to its writer instruction/context, address, old/new byte, sequence/time, and subsequent execution when observed. Preserve multiple writes before execution. | A write–write–execute fixture identifies the executed value and intervening history; it does not claim that the first written value was executed. |
| SMC-03 | Distinguish same-value code writes, actual byte changes, writes that are never subsequently executed, blocked writes, and host-originated modifications. | These five cases produce different truthful records; protected ROM attempts do not appear as successful mutation. |
| SMC-04 | Invalidate affected decoding, instruction spans, flow associations, source mappings, and inferred semantics when bytes change. Retain earlier evidence as history. | Changing an opcode from a short to a longer instruction causes the new executed span to be recorded; old operand flags are not presented as proof of the current interpretation. |
| SMC-05 | Show the modifying instruction, affected instruction or region, before/after bytes, and before/after disassembly only where the complete required byte versions are available. Mark unavailable historical decoding explicitly. | A multi-byte rewrite can be inspected accurately; replacing one old byte in today's unrelated surrounding bytes is not presented as a verified past instruction. |
| SMC-06 | Preserve break-on-code-write and add user-selectable stopping on first execution of newly written/changed code, with clear timing of each stop. | Tests stop at the documented boundary and identify whether the event is a completed write or impending/observed execution; CPU state matches that explanation. |
| SMC-07 | Let users inspect mutation history and associate stable roles/names with changing code while limiting each interpretation to its valid context. | Multiple observed operand values and alternative instruction forms can be explored without erasing a user's routine annotation or merging incompatible code meanings. |
| SMC-08 | Export a deliberately selected version of mutable code: as loaded, a named capture, or another retained version. Never imply one listing represents every runtime state. | The export identifies its byte baseline and mutation caveats; verification compares that baseline rather than a changing live region. |

### 5.8 Durable projects, session recovery, and source reconstruction — M3–M5

| ID | Requirement | Acceptance evidence |
|---|---|---|
| DUR-01 | Save and reopen a portable reverse-engineering project containing artifact identities, captures/references, names, comments, data declarations, roles, accepted/rejected proposals, provenance, and relevant build configuration. | After application restart, accumulated understanding and its evidence associations are recoverable without manual re-entry. |
| DUR-02 | Support a separately identified resumable session with CPU/RAM, machine/device state required for continuation, breakpoints, relevant debugger settings, and tape position when applicable. State what cannot be resumed faithfully. | A save/resume fixture continues from an equivalent observable state; missing ROM/tape assets or unsupported state prevent a false claim of exact continuation. |
| DUR-03 | Detect missing or changed binaries, ROMs, source, toolchains, and captures during reopen/import. Preserve the saved knowledge while marking incompatible associations. | Substituting a different binary at the same path triggers a mismatch; no automatic reattachment by filename/address alone occurs. |
| DUR-04 | Make project content versioned, inspectable, and suitable for source control; preserve legacy `.sym` import/export where representable. Save failures must not destroy the last valid project. | An annotation change produces a reviewable change; round-trip save/reopen preserves content; truncation/invalid-version fixtures fail with recovery guidance. |
| EXP-01 | Produce documented listings and assembly for selected captures/ranges using the chosen dialect: labels, comments, origins, constants, instructions, and data representations. Preserve unknown bytes explicitly. | A mixed code/data fixture exports without inventing semantics or discarding unexplored bytes. |
| EXP-02 | Support partial exports with explicit treatment of references outside the selection, gaps, overlapping interpretations, unsupported instruction spellings, and mutable regions. | External targets resolve as defined imports/equates or a reported limitation; encodings without an exact supported mnemonic can be preserved as raw bytes. |
| EXP-03 | Reassemble exported source with the identified toolchain and compare every byte in the declared verification range to the selected capture, including missing/extra bytes and origin mismatches. | A correct fixture is byte-exact; deliberate byte, size, and placement errors are detected and mapped to addresses and source locations where available. |
| EXP-04 | Emit a portable verification report naming capture/artifact hashes, ranges, dialect/toolchain, result, mismatches, exclusions, and unresolved interpretation. Enable the verified export to re-enter the build/deploy workflow. | A second run can repeat the comparison; the report separates byte-exact reconstruction from any additional observed behavioral comparison. |

### 5.9 Quality, reliability, and release readiness — all milestones

| ID | Requirement | Acceptance evidence |
|---|---|---|
| QLT-01 | Define a reference host, workload corpus, response-time targets, trace-retention budget, and tolerated instrumented overhead before release. Report measured behavior separately from targets. | A reproducible measurement report covers representative project edits, full 64 KB listings, sustained Spectrum execution, and enabled collection modes. |
| QLT-02 | Keep editing responsive during emulation and bounded analysis; support interruption/progress for long builds, imports, exports, and verification. | Stress fixtures remain interactive and cancellation reaches a documented stable state without partial deployment or lost annotations. |
| QLT-03 | Preserve CPU behavior and emulated results with collection disabled/enabled; retain the bare-core performance invariant. | Identical deterministic fixtures agree on observable state and emulated timing; benchmark comparisons use the same host/build settings. |
| QLT-04 | Publish supported desktop OS, VS Code, server, emulator, assembler, and project-format compatibility; provide setup and recovery instructions. | Installation and complete sample workflows pass on each platform actually claimed; unsupported combinations fail visibly. |
| QLT-05 | Make build, binary load, capture, analysis evidence export, project save/load, source export, and byte verification reproducible headlessly. | Automation can perform and assess the required workflow without GUI interaction and with meaningful exit results/artifacts. |
| QLT-06 | Require behavioral evidence for every claimed milestone, including failure and conflict cases. Preserve the distinction between source inspection, existing tests, prototype demonstrations, and release acceptance. | A milestone report links its requirement IDs to reproducible results and lists every deferred, failed, or skipped scenario. |

## 6. Delivery sequence and relationship to the existing roadmap

This sequence adds release gates to the existing roadmap; it does not erase machine correctness, compatibility, tape, configuration, or UI work. Milestones express dependencies rather than calendar estimates. No staffing or delivery dates are assumed.

| Milestone | Deliverable and scope | Dependencies | Exit gate | Existing roadmap relationship |
|---|---|---|---|---|
| **M0 — Establish the contract** | Review GOV-01–08; resolve first dialect, supported targets, capture/session semantics, and measurement criteria. Reconcile documentation with code. | Current baseline | Recorded decisions and acceptance fixtures are sufficient to start bounded implementation. | Retains current architecture and correctness priorities. |
| **M1 — Assembly editing** | LSP-01–08; VSC-01/02/04/08 for editing, configuration, and diagnostics. Runtime links may truthfully be unavailable. | M0 | A multi-file project works in VS Code and an independent protocol client, including safe rename and invalidation cases. | New LSP/VS Code requirements extend the development-workbench objective. |
| **M2 — Build, deploy, run** | BLD-01–06; RUN-01–08; VSC-03/05/06/08 for live control. | M0; VS Code release integrates M1 | Build in VS Code, deploy into an existing instance, stop at source/address, inspect state, edit, rebuild, and repeat. Unsafe and stale loads fail cleanly. | Makes “Now: development workbench,” binary insert, CLI parity, and L8 concrete. L8 need not wait for advanced L7 inference. |
| **M3 — Preserve understanding** | SEM-01–07; DUR-01–04; VSC-07 for captures and annotations. | M0 and existing decoder/symbols; integrate M2 artifact identities | Binary-only exploration, progressive naming, capture, save, close, reopen, and session continuation pass with mismatch handling. | Extends existing `.sym`; delivers L3, essential L4, and current session-save priorities. |
| **M4 — Explain observed behavior** | SEM-08; EVT-01–08; SMC-01–08; VSC-07 for evidence views. Export-facing SMC-08 completes with M5. | M3 identity/durability; reliable execution/frame context | Exact-count, frame-loop, write-then-execute, changed-length, history-limit, and user-refinement scenarios pass. | Extends L1/L2 and L7. Adds explicit frame-loop reasoning and temporal semantics missing from current implementation. |
| **M5 — Recover verifiable source** | EXP-01–04; SMC-08 integration; complete bidirectional workflow. | M2 toolchain/deployment and M3 captures/annotations; M4 for historical SMC analysis | Selected mixed and mutable captures export and reassemble byte-exactly; deliberate mismatches are localized. Rebuilt source deploys through the same validated path. | Completes L4–L6 and L9; turns the original RAM-to-source objective into an acceptance gate. |
| **M6 — Release and broaden** | Complete QLT-01–06 across all shipped features; packaging, documentation, portability, and supported-platform validation. | Required M1–M5 gates | A fresh installation completes both source-first and binary-first workflows on every claimed platform, with reproducible evidence. | Preserves “Later” additional dialects, richer packaging, and snapshot interchange as subsequent increments. |

QLT-02/03/05/06 and applicable GOV requirements apply from the first implementation milestone; M6 is not a deferral of correctness or testability. M1, the headless portion of M2, and early M3 work can progress independently once M0 decisions are sufficient. Higher-level inference must not block a usable build/deploy loop or manual annotation workflow. Basic frozen-capture export can be developed before all inference features are complete.

### Existing work that remains alongside this sequence

The current roadmap's CPU-suite maintenance, TZX flow semantics, contention modeling and tests, floating-bus calibration, golden-screen capture, settings/load-save flows, tape controls, and later audio regression remain valid work. [R1] They are not all dependencies for an assembly LSP. Machine accuracy defects that invalidate execution or timing evidence become blockers for the affected acceptance claims. In particular, frame-correlation can use the emulator's frame clock before contention is complete, but raster-accurate behavior must not be certified on that basis.

## 7. End-to-end acceptance scenarios

Each scenario must retain its fixture, effective configuration, expected result, and actual result. Numeric examples below define synthetic tests, not measured properties of an existing game.

| Scenario | Procedure and required result | Principal requirements |
|---|---|---|
| **A1 — Edit a real project** | Open a multi-file program with includes, scoped labels, a macro, and conditional assembly. Introduce/fix an error and rename a symbol. Navigation and version-aware edits remain correct in VS Code and a protocol client. | LSP-01–08; VSC-01/02/04 |
| **A2 — First source-to-machine loop** | Build a small program with a known RAM result and Spectrum border effect. Deploy at an explicit RAM origin, set PC/SP/interrupt policy, run to a mapped breakpoint, and verify bytes/registers/result. Change the source and repeat in the same instance. | BLD-01–06; RUN-01–08; VSC-03/05/06 |
| **A3 — Refuse unsafe deployment** | Attempt an overflow at the top of memory, protected-ROM load, conflicting origin, stale output, missing tool, disconnected target, and cancelled build. Each names its failure and leaves no partially executing program. | BLD-03/04; RUN-01–04; VSC-08 |
| **A4 — Binary-first exploration** | Insert an unnamed binary, execute a selected path, inspect code/unknown regions, label a routine and data, add comments, save, close, reopen, and continue. All user work and its capture identity survive. | RUN-06; SEM-01–07; DUR-01–04 |
| **A5 — Exact counts and flow** | Execute a fixture with a loop body reached exactly 100 times, both conditional outcomes, a computed jump, and a repeated block operation. Counters/edges match the defined counting contract in normal, turbo, and stepped runs. | EVT-01–03/07/08 |
| **A6 — Frame roles without overclaiming** | Observe 120 complete synthetic frames: an outer loop entry once per frame, an inner loop 32 times per frame, and a separate interrupt handler. Show the distributions and supporting flow. The user assigns `game_loop`; a later variable-work phase retains the name and adds contrary evidence. Pause mid-frame and verify no duplicate frame. | EVT-04–06; SEM-06; RUN-08 |
| **A7 — Two directions of code mutation** | Execute an instruction, patch its operand, and execute it again. Separately write a never-executed routine then jump into it. Both histories are captured with correct writer/value/execution relationships and different classifications. | SMC-01/02/06/07 |
| **A8 — Temporal boundary correctness** | Rewrite a one-byte instruction into a multi-byte instruction, make two writes before its next execution, and include same-value and blocked-ROM writes. Decode/count the actual new span and preserve prior context without claiming that every written version ran. | SMC-02–05; EVT-01/02 |
| **A9 — Preserve identity through conflict** | Rename a recovered routine, then rebuild/reload changed code at the same address and import conflicting symbols. User knowledge remains recoverable; incompatible mappings are flagged and no silent overwrite occurs. | GOV-03/08; BLD-05; RUN-05/07; SEM-05/06; DUR-03 |
| **A10 — Byte-exact reconstruction** | Capture a mixed instruction/data region with outside references and an encoding needing raw-byte preservation. Annotate, export, assemble, and verify exact bytes/ranges. Deliberately alter one byte and the origin to prove both mismatches are detected and localized. | EXP-01–04 |
| **A11 — Mutable-code reconstruction** | Retain as-loaded and post-mutation captures, export each explicitly, and verify each against its own baseline. Re-deploy one verified artifact; any behavioral comparison is reported separately from byte equality. | SMC-05/08; EXP-03/04; RUN-05 |
| **A12 — Recovery and retention limits** | Exceed trace retention, interrupt a save, reopen with a missing asset, disconnect the emulator, and exercise conflicting UI/client commands. Preserve prior valid work and label unavailable state/history. | EVT-08; DUR-02–04; RUN-08; VSC-08; QLT-02 |
| **A13 — Reproducible release** | Repeat A2, A4, and A10 on a fresh supported installation and headlessly; compare instrumented/uninstrumented fixture results and benchmark the bare core. Record performance and all asset skips. | GOV-01/07; QLT-01–06 |

## 8. Product decisions, deferred decisions, and open questions

### Product decisions made in this draft

- Cover both source-first development and binary-first understanding; neither requires the other as its starting point.
- Prove one explicit dialect and a local desktop workflow before expanding toolchains or machine models.
- Permit a safe pause during live deployment; retain the running emulator instance.
- Preserve manual symbol refinement as a first-class workflow even when automatic inference is incomplete.
- Make knowledge provenance, conflicting evidence, temporal code identity, persistence, and byte verification release requirements.
- Extend the existing workbench and L1–L9 roadmap rather than describing already implemented coverage/SMC as new work.

These are proposed scope and acceptance choices. Technical realization remains open.

### Decisions explicitly deferred

Server implementation language, parser/grammar technology, reuse of another LSP project, repository/package boundaries, transport and discovery mechanisms, DAP versus other debugger integration, thread/process ownership, exact instrumentation interfaces, semantic storage/schema, project extension/serialization, confidence scoring algorithms, control-flow/routine detection algorithms, and distribution/update mechanisms require technical design. Existing JSON `.sym` support and the reverse-engineering roadmap's JSON project sketch are compatibility context, not approval of a new schema.

### Questions for the CTO / project owner

| Decision | Open question and recommended starting position | Needed before |
|---|---|---|
| D1 — First dialect | **Resolved in part:** Pasmo is the authoritative first assembler and dialect. The exact supported version and conformance corpus remain to be approved and validated. | M1 dialect acceptance; M2 build adapter |
| D2 — Supported release targets | Which desktop operating systems and VS Code versions must the first release support? Publish a tested matrix rather than imply all platforms. | Packaging and QLT acceptance commitments |
| D3 — LSP ownership/reuse | **Resolved:** `lsp-z80` is a top-level subproject in this repository. It retains an independently testable Python package, stdio process boundary, and nested VS Code client. | Technical design and dependency commitments |
| D4 — Runtime experience | Are VS Code native debugger controls required, or are equivalent extension controls acceptable initially? Retain the existing ImGui debugger and consistent behavior either way. | VS Code runtime integration design |
| D5 — Start and restore semantics | What are the first default load/reset/PC/SP/interrupt policies, and which machine/device states must resume exactly? Recommend explicit preserve/reset presets and no unsupported exact-resume claim. | M2 deployment and M3 session design |
| D6 — Performance envelope | What reference hardware, project size, response-time objectives, retention duration, and overhead limits define release readiness? Set measurable thresholds before implementation acceptance; do not inherit unmeasured “negligible cost” claims. | QLT-01 acceptance plan |
| D7 — Evidence coverage | Which execution paths and event precision are required initially? Recommend all shipped debugger run/step modes, exact aggregate counts, explicit trace gaps, and no bus-cycle-accurate timestamp claim without validation. | M4 collection and reporting design |

### Risks and disconfirmation

| Risk | Evidence that would expose it | Required response |
|---|---|---|
| Dialect disagreement creates convincing but wrong navigation or exports. | The supported assembler disagrees with source resolution or generated bytes on the fixture corpus. | Narrow the supported profile or fix semantics before claiming that capability. |
| Current address-based coverage is mistaken for current code truth after mutation. | A changed-length instruction retains an old span or old source mapping. | Block mutable-code acceptance until version invalidation and execution evidence agree. |
| Frame frequency is mistaken for program purpose. | An ISR or render helper is labeled game loop solely from one-per-frame counts. | Show multiple candidates and evidence; preserve user judgment and revisability. |
| Observation becomes expensive or incomplete. | Frame pacing degrades, traces overflow silently, or instrumented results differ. | Measure and bound collection, expose gaps, and protect correctness/performance gates. |
| Durable knowledge drifts away from its binary. | Reopening changed files retains unqualified names/evidence at old addresses. | Require identity checks and explicit reconciliation. |
| Successful byte reconstruction is mistaken for recovered intent or behavior. | A byte-exact export includes speculative labels or captures only one mutable phase. | Limit the claim to verified bytes/ranges and report semantic/runtime uncertainty separately. |

## 9. Review and sign-off

**Recommended next step:** Review requirements and resolve D1–D7 to the extent needed for M0. Then commission technical design for the first bounded milestones, using the pinned code baseline and acceptance scenarios. Implementation can be staged without waiting for all advanced inference decisions.

- **Approval status:** Draft; submitted for requirements review.
- **Architect review:** Not performed. Requested focus: compatibility with current boundaries, completeness of state/identity semantics, and separation of editing from runtime control.
- **CTO review:** Pending. No approval inferred from the earlier conversation or existing roadmap sketches.
- **Author:** Codex, requirements author (agent).
- **Author disposition:** Submitted for review; repository-grounded requirements and sequencing complete.
- **Author date:** 5 September 2026.
- **Review entries:** None recorded.
- **CTO sign-off:** Human project owner; pending.
- **Workflow status:** Requirements draft; implementation and release acceptance remain future work.

## 10. Source register

Repository links are pinned to the inspected commit so future changes do not alter the evidence behind this draft. R references support baseline statements and existing priorities; the numbered requirements are newly proposed obligations, not claims copied from implementation.

- **R1 — [Current development roadmap](https://github.com/dawsonlp/z80-digital-twin/blob/928ef0328377d3dce346e38f3d1840894cd00b97/docs/developers/roadmap.md).** “Now / Next / Later” priorities and source-to-machine direction.
- **R2 — [Reverse-engineering laboratory roadmap](https://github.com/dawsonlp/z80-digital-twin/blob/928ef0328377d3dce346e38f3d1840894cd00b97/docs/developers/reverse-engineering-roadmap.md).** Existing L1–L9, annotation/export goals, toolchain candidates, and caveats.
- **R3 — [Architecture](https://github.com/dawsonlp/z80-digital-twin/blob/928ef0328377d3dce346e38f3d1840894cd00b97/docs/developers/architecture.md).** Policy model, dependency boundaries, and bare-core performance invariant.
- **R4 — [CMake build definitions](https://github.com/dawsonlp/z80-digital-twin/blob/928ef0328377d3dce346e38f3d1840894cd00b97/CMakeLists.txt#L59).** Actual libraries, sources, optional GUI dependencies, and test registration.
- **R5 — [Debug session interface](https://github.com/dawsonlp/z80-digital-twin/blob/928ef0328377d3dce346e38f3d1840894cd00b97/debugger/exec/debug_session.h#L42) and [implementation](https://github.com/dawsonlp/z80-digital-twin/blob/928ef0328377d3dce346e38f3d1840894cd00b97/debugger/exec/debug_session.cpp#L44).** Execution, coverage, SMC, blocked writes, event cap, and reset semantics.
- **R6 — [Observable memory](https://github.com/dawsonlp/z80-digital-twin/blob/928ef0328377d3dce346e38f3d1840894cd00b97/src/memory/observable_memory.h#L29).** Write observations, protection, and direct-write bypass.
- **R7 — [Disassembler interface](https://github.com/dawsonlp/z80-digital-twin/blob/928ef0328377d3dce346e38f3d1840894cd00b97/debugger/disasm/disassembler.h#L37).** Decoded instruction data and static target information.
- **R8 — [Symbol table](https://github.com/dawsonlp/z80-digital-twin/blob/928ef0328377d3dce346e38f3d1840894cd00b97/debugger/symbols/symbol_table.h#L46) and [symbol editing UI](https://github.com/dawsonlp/z80-digital-twin/blob/928ef0328377d3dce346e38f3d1840894cd00b97/debugger/ui/symbol_edit.cpp#L40).** Current knowledge and naming support.
- **R9 — [Debugger application](https://github.com/dawsonlp/z80-digital-twin/blob/928ef0328377d3dce346e38f3d1840894cd00b97/debugger/ui/debugger_app.cpp#L72), including [Spectrum frame driving](https://github.com/dawsonlp/z80-digital-twin/blob/928ef0328377d3dce346e38f3d1840894cd00b97/debugger/ui/debugger_app.cpp#L242).** Actual file loading, symbols, reset, and pause/resume behavior.
- **R10 — [CPU program loader](https://github.com/dawsonlp/z80-digital-twin/blob/928ef0328377d3dce346e38f3d1840894cd00b97/src/z80_cpu.cpp#L267).** Current bounded byte-copy primitive.
- **R11 — [Machine frame clock](https://github.com/dawsonlp/z80-digital-twin/blob/928ef0328377d3dce346e38f3d1840894cd00b97/machine/machine.h) and [Spectrum timing constants](https://github.com/dawsonlp/z80-digital-twin/blob/928ef0328377d3dce346e38f3d1840894cd00b97/machine/spectrum/timing.h).** Existing time bases and machine scheduling.
- **R12 — Tests inspected and run: [debug session](https://github.com/dawsonlp/z80-digital-twin/blob/928ef0328377d3dce346e38f3d1840894cd00b97/tests/debug_session_test.cpp), [disassembler](https://github.com/dawsonlp/z80-digital-twin/blob/928ef0328377d3dce346e38f3d1840894cd00b97/tests/disassembler_test.cpp), [symbols](https://github.com/dawsonlp/z80-digital-twin/blob/928ef0328377d3dce346e38f3d1840894cd00b97/tests/symbol_table_test.cpp), [machine](https://github.com/dawsonlp/z80-digital-twin/blob/928ef0328377d3dce346e38f3d1840894cd00b97/tests/machine_test.cpp), [observable memory](https://github.com/dawsonlp/z80-digital-twin/blob/928ef0328377d3dce346e38f3d1840894cd00b97/tests/observable_memory_test.cpp).** Current focused regression evidence; new acceptance scenarios have not been implemented or run.
- **R13 — [Repository status](https://github.com/dawsonlp/z80-digital-twin/blob/928ef0328377d3dce346e38f3d1840894cd00b97/docs/reference/status.md).** Reported working areas and known fidelity gaps.
- **P1 — [Language Server Protocol specification](https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/).** Reference compatibility baseline; exact supported version remains a release decision.
- **P2 — [VS Code language server extension guide](https://code.visualstudio.com/api/language-extensions/language-server-extension-guide).** Editor/client and language-server roles.
- **P3 — [VS Code debugger extension guide](https://code.visualstudio.com/api/extension-guides/debugger-extension).** Debugger integration context and DAP option.
