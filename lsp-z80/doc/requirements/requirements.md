# Z80 Assembly Language Server Requirements

## Purpose

This document defines the product requirements for a Language Server Protocol (LSP) server that gives editors accurate, responsive language assistance for Z80 assembly source code. It states the outcomes the server must provide, the constraints it must respect, and the evidence required for acceptance. It intentionally does not prescribe internal architecture, libraries, parsing strategy, storage, or code structure.

This is a draft for review. Pasmo is the approved first assembler dialect. Requirements that depend on an exact Pasmo version range, processor profile, or editor choice remain conditional until the decisions in **Questions for CTO** are resolved.

## Product Goal

Enable a programmer to understand, navigate, and change a non-trivial Z80 assembly project from an LSP-capable editor without having to use text search as the primary semantic tool and without receiving misleading results caused by an unrecognized assembler dialect.

## Users and Stakeholders

- **Assembly author:** writes and debugs Z80 assembly and needs immediate, source-located feedback.
- **Project maintainer:** navigates unfamiliar code, follows symbols across files, and performs bounded refactoring.
- **Build/tool integrator:** configures the language server to match the project's assembler, include paths, and predefined symbols.
- **Editor integrator:** connects a standards-compliant LSP client and needs declared, interoperable capabilities.
- **Language-server maintainer:** needs observable, reproducible behavior and a conformance corpus.

## Scope

### In scope for the first usable release

- Standards-compliant LSP lifecycle and document synchronization over standard input/output.
- Analysis of the approved baseline Z80 instruction set using the Pasmo assembler dialect.
- Single-file and workspace-aware diagnostics.
- Contextual completion, hover information, navigation, references, document/workspace symbols, highlighting, and semantic tokens.
- Safe, semantically constrained symbol rename where correctness can be established.
- Project configuration for dialect-sensitive behavior, include resolution, source discovery, and predefined symbols.
- Tolerant analysis of incomplete documents during editing.
- Clear reporting when source uses unsupported, ambiguous, or unconfigured language features.
- Automated evidence for protocol interoperability, language correctness, responsiveness, and failure handling.

### Future scope, not required for the first usable release

- Additional Z80-family processors or extensions, such as Z180, eZ80, Rabbit, Game Boy LR35902, or undocumented opcodes.
- Additional assembler dialects beyond Pasmo.
- Source formatting and style enforcement.
- Code actions or automated rewrites beyond a safe symbol rename.
- Call hierarchy, type hierarchy, inlay hints, or generated disassembly views.
- Direct integration with assemblers, linkers, emulators, debuggers, or build systems.
- Debug Adapter Protocol support.
- Hosted, remote, or collaborative indexing.

### Out of scope

- Acting as an assembler, linker, emulator, debugger, decompiler, or binary-analysis system.
- Guaranteeing that a program is functionally correct or cycle-accurate.
- Executing source files, macros, build scripts, assembler binaries, or shell commands as part of normal language analysis.
- Silently guessing a dialect when doing so would change parsing or symbol meaning.
- Editing files directly without an editor-mediated, version-aware LSP operation.
- Providing semantic claims for syntax the server cannot interpret with sufficient confidence.

## Definitions

- **Core Z80:** the documented Zilog Z80 programmer-visible instruction set and registers, excluding later CPU-family extensions and undocumented instructions unless separately approved.
- **Dialect:** an assembler's accepted syntax and semantics, including directives, literal forms, label rules, macro syntax, conditional assembly, and operator precedence.
- **Workspace:** the roots and configuration supplied by the LSP client during initialization or workspace-folder changes.
- **Open document:** a document whose current contents and version have been supplied by the client; these contents are authoritative over the saved file.
- **Symbol:** a source-defined name with meaning under the selected dialect, such as a label, constant, macro, section, or alias.
- **Resolvable expression:** an expression whose value can be determined from available source and configuration without executing external tools.
- **Diagnostic confidence:** the server's basis for treating a finding as an error, warning, information item, or unsupported/unknown construct.
- **First usable release:** the smallest approved release that satisfies all Must requirements and the acceptance gates in this document.

## Priority Convention

- **Must:** required for the first usable release.
- **Should:** expected unless review accepts a documented reason to defer it.
- **Could:** useful but not required for first-release acceptance.

## Functional Requirements

### Protocol lifecycle and interoperability

- **LSP-001 — Must:** The server shall communicate using a stable, approved version of the Language Server Protocol and JSON-RPC framing expected by conforming LSP clients.
- **LSP-002 — Must:** The server shall support initialization, capability negotiation, initialized notification, orderly shutdown, and exit behavior without requiring a client-specific extension.
- **LSP-003 — Must:** The server shall advertise only capabilities it implements and shall vary behavior according to relevant client capabilities.
- **LSP-004 — Must:** The server shall use zero-based LSP positions and the character-position encoding negotiated with the client.
- **LSP-005 — Must:** Protocol output shall not be contaminated by logs or human-readable status text.
- **LSP-006 — Must:** Unknown notifications shall not terminate the server. Unsupported requests shall receive a valid protocol error.
- **LSP-007 — Must:** Requests that can become obsolete or expensive shall honor client cancellation when the protocol permits it.
- **LSP-008 — Should:** The server shall support workspace-folder additions and removals during a session when the client declares that capability.
- **LSP-009 — Should:** The server shall produce interoperable results in at least two approved, independently implemented LSP clients.

### Document and workspace state

- **DOC-001 — Must:** The server shall track open, changed, saved, and closed text documents according to the synchronization capability it advertises.
- **DOC-002 — Must:** Analysis of an open document shall use the latest client-supplied version, even when the saved file differs.
- **DOC-003 — Must:** Results tied to a document version shall not be presented as current after a newer version supersedes them.
- **DOC-004 — Must:** The server shall handle LF and CRLF line endings and files with or without a final newline.
- **DOC-005 — Must:** The server shall accept a documented set of source filename extensions and allow projects to configure additional extensions.
- **DOC-006 — Must:** The server shall analyze source reachable through workspace files and configured include roots, subject to access constraints.
- **DOC-007 — Must:** File creation, change, and deletion reported by the client shall invalidate affected results.
- **DOC-008 — Must:** Closing an unsaved document shall cause subsequent analysis to reflect the saved file, or the file's absence, rather than stale unsaved content.
- **DOC-009 — Should:** Multi-root workspaces shall preserve the configuration and symbol context of each root.
- **DOC-010 — Should:** The server shall identify ambiguous ownership when a source file could belong to multiple workspace roots rather than silently selecting a materially different configuration.

### Language recognition

- **LANG-001 — Must:** The server shall recognize the complete approved core Z80 mnemonic and register set, including legal documented operand combinations.
- **LANG-002 — Must:** Recognition of mnemonics and registers shall follow Z80 conventions for case insensitivity while preserving source spelling in displayed and edited text.
- **LANG-003 — Must:** The server shall recognize Pasmo's label declarations, symbol references, constants, expressions, comments, directives, include forms, macros, and conditional-assembly constructs to the degree claimed in the Pasmo support statement.
- **LANG-004 — Must:** The server shall apply Pasmo's numeric literal forms, character/string literal rules, operator meanings, precedence, and current-location notation.
- **LANG-005 — Must:** The server shall distinguish instruction operands by semantic category where the core language does so, including registers, register pairs, conditions, immediate values, memory indirection, indexed displacement, bit number, restart vector, and port operands.
- **LANG-006 — Must:** The server shall model symbol visibility and qualification rules defined by Pasmo, including Pasmo's global, explicit-local, and configured automatic-local label behavior where included in the approved version scope.
- **LANG-007 — Must:** The server shall account for active and inactive conditional-assembly branches when the controlling expressions are resolvable.
- **LANG-008 — Must:** When conditional state is not resolvable, the server shall avoid definitive semantic errors that assume only one possible branch unless the uncertainty is communicated.
- **LANG-009 — Must:** The server shall resolve source includes using deterministic, documented precedence across the including file, workspace, and configured include roots.
- **LANG-010 — Must:** Include cycles shall be reported without crashing, hanging, or repeatedly emitting equivalent findings.
- **LANG-011 — Must:** The server shall tolerate partially typed instructions, expressions, declarations, macros, and directives and shall continue to provide unaffected results.
- **LANG-012 — Must:** Unsupported syntax shall be distinguishable from invalid syntax.
- **LANG-013 — Should:** Pasmo macro definitions and invocations shall participate in navigation and symbol results where their semantics can be determined reliably.
- **LANG-014 — Should:** Source regions disabled by resolvable conditional assembly shall be represented distinctly in semantic presentation.
- **LANG-015 — Could:** The server may expose instruction timing or size information when the selected processor profile and instruction form make it unambiguous and the provenance is documented.
- **LANG-016 — Must:** The canonical hexadecimal literal form shall be a dollar-sign prefix followed by one or more hexadecimal digits, such as `$2a` or `$8000`. The dollar sign by itself remains Pasmo's current-location operator and is not a hexadecimal literal.
- **LANG-017 — Must:** Hexadecimal forms accepted by the approved Pasmo version range but not matching the canonical `$`-prefixed form shall remain syntactically valid and shall retain their correct numeric value during semantic analysis.
- **LANG-018 — Must:** Server-generated examples, completion details, hover renderings, and any other server-generated representation of hexadecimal values shall use the canonical `$`-prefixed form unless reproducing source text verbatim.
- **LANG-019 — Must:** Server-generated mnemonics, directives, registers, and hexadecimal digits shall use lowercase. Recognition remains case-insensitive, and existing source spelling shall not be rewritten merely to enforce presentation preference.
- **LANG-020 — Must:** Symbol hover shall identify the item simply as a symbol and show a source-faithful preview beginning at its definition. The preview shall contain at most three source lines, preserve comments, stop at a blank line, a following label, or an unambiguous return, and append an ellipsis only when additional contiguous definition content was omitted by the line limit. Jumps shall not truncate the preview. It shall not claim an assembled value or address unless assembly-backed evaluation is separately implemented and validated.
- **LANG-021 — Must:** The `z80` editor language identity shall remain distinct from assembler dialect selection. Clients shall pass an explicit dialect request during initialization; unsupported choices shall be rejected rather than silently interpreted as Pasmo. Any future automatic selection shall require unambiguous source evidence and expose the selected dialect.

### Diagnostics

- **DIAG-001 — Must:** The server shall publish diagnostics with an accurate source range, severity, stable diagnostic code, concise message, and human-readable source identifier.
- **DIAG-002 — Must:** Diagnostics shall update after relevant document, dependency, or configuration changes and stale diagnostics shall be cleared.
- **DIAG-003 — Must:** The server shall diagnose unrecognized mnemonics under the active processor/dialect profile.
- **DIAG-004 — Must:** The server shall diagnose invalid operand count and invalid operand combinations for recognized core instructions.
- **DIAG-005 — Must:** The server shall diagnose statically provable range violations, including indexed displacements, bit indices, restart vectors, and fixed-width immediate values, while respecting Pasmo's coercion rules.
- **DIAG-006 — Must:** The server shall diagnose duplicate symbol definitions when they conflict under Pasmo's scope rules.
- **DIAG-007 — Must:** The server shall diagnose unresolved symbol references only when analysis has sufficient context to conclude that the symbol is undefined.
- **DIAG-008 — Must:** The server shall report missing, unreadable, and cyclic includes at the initiating include site.
- **DIAG-009 — Must:** The server shall diagnose malformed expressions and unmatched delimiters or conditional blocks without cascading an unbounded number of derivative messages.
- **DIAG-010 — Must:** The server shall not report inactive conditional branches as active program errors when inactivity is known.
- **DIAG-011 — Must:** Diagnostics caused by unsupported dialect features shall say that support is absent or uncertain rather than asserting that valid source is erroneous.
- **DIAG-012 — Must:** The server shall continue diagnostics for unaffected regions after a local syntax error whenever meaningful recovery is possible.
- **DIAG-013 — Should:** Related diagnostic information shall identify a prior definition, include source, or other relevant location when that materially helps resolution.
- **DIAG-014 — Should:** Projects shall be able to disable or change the severity of individually coded diagnostics without changing parsing semantics.
- **DIAG-015 — Should:** Unnecessary or unreachable constructs shall be diagnosed only where the conclusion follows from assembly semantics, not from assumptions about runtime control flow.
- **DIAG-016 — Must:** Each valid but non-canonical Pasmo hexadecimal literal shall receive an accurately ranged, warning-level diagnostic that identifies the literal as non-standard and shows its equivalent canonical `$`-prefixed spelling. The diagnostic shall have a stable code and may be explicitly disabled or reclassified under DIAG-014.

### Completion and signature assistance

- **COMP-001 — Must:** Completion shall be context-sensitive enough to distinguish instruction, operand, directive, and expression positions.
- **COMP-002 — Must:** Instruction completion shall offer legal core mnemonics and identify them as instructions.
- **COMP-003 — Must:** Operand completion shall prioritize legal registers, conditions, symbols, or forms for the current instruction and operand position.
- **COMP-004 — Must:** Symbol completion shall respect Pasmo's visibility and qualification rules and shall distinguish relevant symbol kinds.
- **COMP-005 — Must:** Directive completion shall reflect Pasmo rather than a union of incompatible assembler syntaxes.
- **COMP-006 — Must:** Completion replacement ranges shall preserve unrelated source text and shall not duplicate already typed prefixes.
- **COMP-007 — Must:** Completion shall remain useful in an incomplete line and shall not require the document to be otherwise error-free.
- **COMP-008 — Should:** Completion details shall show a concise legal operand pattern or declaration summary.
- **COMP-009 — Should:** Signature help shall show legal operand forms for the active instruction and indicate the active operand.
- **COMP-010 — Could:** Completion may provide opt-in snippets, but plain insertion text shall remain available to clients that do not support snippets.

### Hover and source information

- **HOVER-001 — Must:** Hovering a recognized instruction shall show its canonical mnemonic, legal operand form relevant to the source, and a concise description.
- **HOVER-002 — Must:** Hovering a core register or condition shall identify its Z80 meaning.
- **HOVER-003 — Must:** Hovering a resolvable symbol shall identify it as a symbol and show the defining source line with its file and line location. It shall not claim an assembled address or evaluated value unless that capability is separately implemented and validated.
- **HOVER-004 — Must:** Hovering an include shall show the resolved target or the reason resolution failed.
- **HOVER-005 — Must:** Hover content shall distinguish source-derived facts from reference information and shall not invent a resolved value when evaluation is uncertain.
- **HOVER-006 — Should:** Hover may show flags affected, encoded size, or timing where authoritative data and the active processor profile make the result reliable.

### Navigation and references

- **NAV-001 — Must:** Go to definition shall navigate from a resolvable symbol reference to its declaration, including across files.
- **NAV-002 — Must:** Go to definition on an include path shall navigate to the resolved source file.
- **NAV-003 — Must:** Navigation shall use the current unsaved document state.
- **NAV-004 — Must:** Ambiguous definitions shall return all valid candidate locations supported by the protocol rather than an arbitrary candidate.
- **NAV-005 — Must:** Find references shall return declarations and uses according to the request's include-declaration setting.
- **NAV-006 — Must:** Reference results shall respect dialect scope, qualification, case rules, and conditional-assembly state.
- **NAV-007 — Must:** Reference results shall not conflate symbols solely because their textual spelling matches.
- **NAV-008 — Should:** Macro invocations, macro definitions, and macro parameters shall support navigation and references where the selected dialect provides reliable identity rules.
- **NAV-009 — Should:** Document highlights shall distinguish read-like references from declarations or write-like definitions where the protocol permits.

### Symbols and structural presentation

- **SYM-001 — Must:** Document symbols shall expose a stable hierarchy or flat structure appropriate to the selected dialect for labels, constants, macros, sections, and other approved declarations.
- **SYM-002 — Must:** Workspace symbol search shall find declarations across analyzed workspace source and return their kind and location.
- **SYM-003 — Must:** Symbol search shall support partial user queries without requiring exact source casing.
- **SYM-004 — Must:** Folding ranges shall be available for supported multiline constructs such as macro bodies, conditional blocks, and explicitly delimited regions when their boundaries are known.
- **SYM-005 — Must:** Semantic tokens shall distinguish at least instructions, registers, labels/symbols, numbers, strings, comments, operators, and assembler directives where the client token model permits.
- **SYM-006 — Must:** Semantic tokens shall remain syntactically valid for the client when source is incomplete or malformed.
- **SYM-007 — Should:** Declaration, definition, readonly, deprecated, or inactive modifiers shall be supplied when justified by the source and supported by the client.
- **SYM-008 — Could:** Selection ranges may be provided for nested expressions, operands, statements, and blocks.

### Rename and edits

- **EDIT-001 — Must:** Rename shall be offered only for symbol kinds whose identity and editable references are reliably known.
- **EDIT-002 — Must:** Rename preparation shall reject invalid source positions, non-renamable language elements, and names invalid under the active dialect.
- **EDIT-003 — Must:** A rename result shall update the intended declaration and all known semantic references without changing unrelated text matches, comments, or string contents.
- **EDIT-004 — Must:** Rename shall preserve symbol qualification and dialect case behavior or reject the operation if preservation cannot be guaranteed.
- **EDIT-005 — Must:** Returned edits shall identify document versions where supported and shall not target files outside approved workspace/configured source roots without explicit client-visible handling.
- **EDIT-006 — Must:** The server shall return edits for the client to apply and shall not directly modify workspace files as a side effect of rename.
- **EDIT-007 — Must:** The server shall reject or clearly warn about rename when unresolved includes, ambiguous conditional state, macro expansion, or unsupported syntax prevents a complete reference set.
- **EDIT-008 — Should:** Rename shall support a previewable workspace edit in clients that expose preview.

### Configuration and user control

- **CFG-001 — Must:** The first usable release shall use Pasmo as its assembler dialect. If a release supports additional dialects, the active dialect shall be explicitly selectable at workspace level and shall not be inferred when inference could change source meaning.
- **CFG-002 — Must:** The active processor profile shall be explicit and shall default only to the approved core Z80 baseline.
- **CFG-003 — Must:** Users shall be able to configure include roots and predefined symbols without editing server source.
- **CFG-004 — Must:** Configuration precedence and scope shall be documented and deterministic.
- **CFG-005 — Must:** Invalid or unknown configuration shall produce an actionable, source-located or client-visible configuration diagnostic and shall not silently select materially different semantics.
- **CFG-006 — Must:** Configuration changes shall invalidate and refresh affected analysis without requiring an editor restart.
- **CFG-007 — Must:** The server shall expose its effective dialect, processor profile, and relevant workspace settings through documented observable behavior or logs.
- **CFG-008 — Should:** Projects shall be able to exclude generated, vendored, binary, or irrelevant paths from workspace analysis.
- **CFG-009 — Should:** Configuration shall allow per-workspace-root overrides in a multi-root session.
- **CFG-010 — Could:** A document-level dialect override may be supported if the identification and precedence rules are explicit.

## Quality Attributes and Constraints

### Correctness and evidence

- **QUAL-001 — Must:** Every claimed instruction/operand form shall be traceable to an approved language reference or assembler-dialect reference.
- **QUAL-002 — Must:** A versioned conformance corpus shall include valid and invalid examples for every core instruction family and supported operand category.
- **QUAL-003 — Must:** The corpus shall cover labels, expressions, directives, includes, macros, conditional assembly, incomplete edits, and cross-file symbol behavior for the selected dialect.
- **QUAL-004 — Must:** Regression cases shall be added for every corrected false positive, false negative, crash, incorrect navigation result, and unsafe edit.
- **QUAL-005 — Must:** Correctness testing shall compare diagnostics and semantic results, not merely confirm that requests complete.
- **QUAL-006 — Should:** Where an approved assembler provides machine-readable or reliably comparable results, representative valid/invalid cases should be checked against it while accounting for intentional scope differences.

### Performance and scale

The following are draft service-level targets and require approval of the reference hardware and corpus before they become release gates.

- **PERF-001 — Must:** On the approved reference corpus of 100,000 source lines, initial workspace-ready analysis shall complete within 5 seconds at the 95th percentile on approved reference hardware.
- **PERF-002 — Must:** After a single-document edit in a warmed workspace, updated diagnostics for the edited document shall be available within 250 milliseconds at the 95th percentile when the edit does not require broad dependency reanalysis.
- **PERF-003 — Must:** Completion and hover shall respond within 150 milliseconds at the 95th percentile in a warmed 100,000-line workspace.
- **PERF-004 — Must:** Definition and document-symbol requests shall respond within 200 milliseconds at the 95th percentile in a warmed workspace; workspace-wide references and symbol search shall respond within 1 second for the approved corpus.
- **PERF-005 — Must:** A canceled long-running request shall stop consuming material work within 250 milliseconds under normal load.
- **PERF-006 — Should:** Steady-state memory use shall remain below 500 MiB for the approved 100,000-line corpus.
- **PERF-007 — Must:** Performance acceptance reports shall identify hardware, client, corpus size, file count, cold/warm state, percentile, and sampling method.

### Reliability and fault isolation

- **REL-001 — Must:** Malformed source, invalid configuration, missing files, include cycles, unknown requests, and canceled requests shall not crash the server.
- **REL-002 — Must:** Failure to analyze one document shall not prevent unaffected documents from receiving service.
- **REL-003 — Must:** Unexpected internal failures shall produce a bounded, actionable error and enough diagnostic logging to reproduce the failure without leaking source by default.
- **REL-004 — Must:** Repeated equivalent events shall not cause unbounded diagnostic duplication, request loops, or resource growth.
- **REL-005 — Must:** The server shall recover correct results after a dependency is created, removed, renamed, or repaired during a session.
- **REL-006 — Should:** A 60-minute stress session using the approved edit/request workload shall complete without a crash, deadlock, protocol corruption, or unbounded memory growth.

### Security, privacy, and workspace boundaries

- **SEC-001 — Must:** The server shall not execute assembly source, macros, included content, build scripts, assembler binaries, or shell commands during normal analysis.
- **SEC-002 — Must:** The server shall not initiate network access for source analysis or reference lookup unless a future feature introduces explicit, documented, opt-in behavior.
- **SEC-003 — Must:** Source text, paths, symbols, and configuration shall remain local unless the user explicitly enables a future external integration.
- **SEC-004 — Must:** Telemetry, if ever present, shall be disabled by default and shall never include source contents, symbol names, or full local paths without explicit informed opt-in.
- **SEC-005 — Must:** File reads shall be limited to workspace roots and explicitly configured include/configuration locations. Attempts to traverse elsewhere shall be rejected or clearly surfaced according to approved policy.
- **SEC-006 — Must:** Logs shall avoid source contents by default and shall support a documented diagnostic mode that makes any increased disclosure clear before use.
- **SEC-007 — Must:** Rename and future edit-producing operations shall be constrained to expected source documents and mediated by the client.

### Accessibility and usability

- **USE-001 — Must:** Diagnostic and configuration messages shall explain what was observed, where it occurred, and what user action can resolve or disambiguate it.
- **USE-002 — Must:** Messages shall not depend on color alone and shall remain understandable in plain text.
- **USE-003 — Must:** The server shall not flood the user with cascading findings from one recoverable syntax error.
- **USE-004 — Must:** Unsupported features and uncertain analysis shall be visibly distinguishable from confirmed errors.
- **USE-005 — Should:** Public documentation shall include a minimal editor-neutral startup example, configuration examples, supported-feature matrix, dialect limitations, troubleshooting guidance, and known incompatibilities.
- **USE-006 — Should:** Features shall behave consistently across supported clients within the limits of each client's advertised capabilities.

### Maintainability and release discipline

- **MAINT-001 — Must:** Each release shall publish the supported LSP version, processor profiles, assembler dialect/version scope, source extensions, and feature matrix.
- **MAINT-002 — Must:** Changes that alter accepted syntax, symbol meaning, diagnostics, configuration, or edits shall include corresponding conformance evidence and user-visible release notes.
- **MAINT-003 — Must:** Diagnostic codes and configuration names shall remain stable within a major release or provide a documented migration path.
- **MAINT-004 — Must:** Experimental features shall be labeled and shall not be advertised as stable protocol capabilities unless they meet normal acceptance gates.
- **MAINT-005 — Should:** Reproducible issue reports shall capture server version, effective profile/dialect, relevant configuration, client identity, and a minimal source example.

## Compatibility Requirements

- **COMPAT-001 — Must:** The server shall work with any conforming LSP client that supports the required baseline protocol features; client-specific adapters may enhance but shall not be required for core behavior.
- **COMPAT-002 — Must:** The release shall state the exact LSP specification version against which it was tested.
- **COMPAT-003 — Must:** Standard input/output transport with standard `Content-Length` framing shall be supported.
- **COMPAT-004 — Must:** The server shall operate without relying on a particular editor's filesystem layout or configuration format.
- **COMPAT-005 — Must:** The tested Pasmo version range shall be documented separately from the core Z80 processor profile.
- **COMPAT-006 — Should:** Linux, macOS, and Windows shall be supported unless review explicitly narrows the first-release platform scope.
- **COMPAT-007 — Should:** Paths, URIs, case sensitivity, and line endings shall follow platform and protocol rules rather than assumptions from the development platform.

## Acceptance Scenarios

The first usable release is acceptable only when all Must requirements are met and the following end-to-end scenarios pass in each approved client and platform combination.

1. **Start and negotiate:** A client starts the server, initializes a workspace, receives truthful capabilities, opens a Z80 source file, and shuts the server down without protocol errors or output corruption.
2. **Edit with incomplete syntax:** A user partially types and then completes an instruction. Completion remains available; temporary diagnostics are bounded; final diagnostics reflect the legal completed instruction within the performance target.
3. **Detect invalid instruction form:** A legal mnemonic with an illegal operand combination receives one accurately ranged diagnostic with a stable code and an actionable message.
4. **Respect the Pasmo boundary:** Valid Pasmo syntax is interpreted according to the approved Pasmo version range. Syntax specific to an unsupported assembler dialect is identified as unsupported rather than silently interpreted using mixed dialect rules.
5. **Navigate across files:** A source file includes another file containing a symbol definition. Definition, references, hover, completion, and workspace symbols resolve the symbol consistently.
6. **Honor unsaved state:** Changing a declaration and its use in unsaved editor buffers updates results without requiring files to be saved.
7. **Handle missing include:** Removing an included file produces a diagnostic at the include, invalidates dependent certainty, and recovers after the file is restored.
8. **Handle conditional assembly:** Known inactive code is distinguished and does not emit active-code errors; unresolved conditions produce appropriately qualified results.
9. **Rename safely:** Renaming a local or global symbol changes exactly its semantic declaration and references across eligible files, preserves unrelated textual matches, and is rejected when completeness cannot be established.
10. **Recover from malformed input:** A corpus of truncated lines, unmatched delimiters, broken macros, invalid encodings handled by policy, and include cycles does not crash or hang the server and still yields results for unaffected source.
11. **Enforce boundaries:** Source attempts to include a path outside approved roots are handled by the documented policy; no source, macro, or build command is executed and no network connection is initiated.
12. **Meet scale targets:** The approved 100,000-line corpus meets cold analysis, warm interaction, cancellation, and memory targets with a reproducible report.
13. **Enforce canonical hexadecimal notation:** `$2a` is accepted without a style diagnostic. Each alternative hexadecimal spelling accepted by the approved Pasmo version range remains semantically valid but receives DIAG-016 with the correct lowercase `$`-prefixed equivalent. Decimal values and the standalone `$` current-location operator are not misclassified.

## Success Criteria

- All Must requirements have automated or explicitly documented acceptance evidence.
- The conformance corpus covers 100% of documented core Z80 instruction/operand forms and all claimed Pasmo constructs, including every accepted hexadecimal literal form and its expected canonical-format diagnostic behavior.
- No known crash, protocol-corruption defect, unsafe rename, or silent dialect misclassification remains open at release.
- In the approved validation corpus, no known false-positive or false-negative diagnostic classified as release-blocking remains open.
- All acceptance scenarios pass in the approved client/platform matrix.
- Performance gates pass using the approved measurement protocol.
- Published documentation clearly distinguishes supported, partial, experimental, and unsupported behavior.

## Inputs Consulted

### Governing Inputs

- User direction dated 2026-09-03: build an LSP server for Z80 assembly language and first produce comprehensive requirements under `doc/requirements/`.
- User direction dated 2026-09-06: use Pasmo as the first dialect; standardize hexadecimal literals on the `$`-prefixed form; accept other Pasmo-valid hexadecimal forms but flag each as non-standard.
- Repository identity: the existing project is named `lsp-z80`.

### Supporting Context

- Existing `pyproject.toml`, which currently declares a minimal Python project but no product behavior.
- Language Server Protocol operating constraints: client-managed lifecycle, capability negotiation, synchronized document versions, zero-based negotiated positions, JSON-RPC `Content-Length` framing, protocol-only standard output, client-applied workspace edits, and orderly shutdown.
- General Z80 language characteristics and the known incompatibility among assembler dialects. Exact authoritative references must be approved before language conformance claims are accepted.

## Governing Constraints

- The product is an LSP server for Z80 assembly; it is not a generic source-editing service.
- Observable behavior must reflect actual source, selected dialect, configured workspace, and current client document versions.
- The server must not claim capabilities or semantic certainty it cannot support.
- Language correctness and safe edits take precedence over feature count.
- External effects, including filesystem edits, command execution, network access, and telemetry, must not occur implicitly.
- The approved Pasmo version range, processor scope, protocol version, clients, platforms, reference corpus, and performance hardware must be made explicit before release acceptance.
- `$`-prefixed hexadecimal is the sole canonical hexadecimal notation. Other Pasmo-valid hexadecimal notations are compatibility input, not additional standards.

## Decisions Made

- The first usable release targets documented core Z80 rather than automatically including related processors or undocumented opcodes.
- Pasmo is the first assembler dialect and must be supported end to end; Pasmo-specific syntax may not be treated as universal Z80 syntax.
- The canonical hexadecimal form is `$` followed by hexadecimal digits. Other Pasmo-valid hexadecimal forms remain accepted for compatibility but are diagnosed as non-standard and are not generated by the server.
- The server must deliver both immediate editor assistance and workspace-level semantic navigation.
- Rename is in scope only with conservative correctness gates and client-mediated edits.
- Formatting, build execution, emulation, debugging, and binary analysis are not first-release requirements.
- Evidence must validate semantic results and failure behavior, not merely process startup or protocol response.

## Decisions Explicitly Deferred

- Implementation language, runtime version, dependencies, LSP framework, parsing technique, indexing strategy, cache format, concurrency model, and process structure.
- The exact supported Pasmo version range.
- Whether undocumented Z80 opcodes or non-Z80 processors enter a later profile.
- The exact stable LSP specification version to claim.
- Client packaging, editor extensions, installation channels, and release artifact formats.
- Configuration file name, schema, discovery rules, and whether configuration is client-supplied, file-backed, or both.
- Diagnostic engine, persistence, logging implementation, benchmark harness, and testing frameworks.
- The internal representation of syntax, symbols, expressions, source relationships, and instruction reference data.

## Assumptions Requiring Validation

- Core Z80 plus the Pasmo dialect is sufficient to produce a useful first release.
- Cross-file includes and symbols are common enough that single-file-only analysis would be inadequate.
- Conservative refusal of rename is preferable to a plausible but incomplete edit.
- A 100,000-line reference workspace is representative of the upper end of the initial target projects.
- The proposed latency and memory targets are achievable on ordinary developer hardware without compromising correctness.

## Open Questions

- Which Pasmo versions are used by the intended first users, and what version range must the first release support?
- Are projects expected to mix dialects or processor profiles within one workspace?
- Which file extensions and project layout conventions occur in real target repositories?
- How should external SDK/include directories be authorized without weakening workspace boundaries?
- Are assembler-generated symbols, linker symbols, or command-line definitions required for correct everyday analysis?
- What source encodings beyond UTF-8, if any, must be supported?
- Which client behaviors differ enough to require a client-specific companion extension?
- What false-positive rate is acceptable for diagnostics when conditional values or macro effects cannot be resolved?
- Is a conservative first release without formatting and code actions sufficient for adoption?

## Questions for CTO

1. Approve the exact Pasmo version range for the first release based on intended user projects and the syntax accepted by those versions.
2. Approve the processor boundary: documented Z80 only, documented plus undocumented opcodes, or named additional processor profiles.
3. Approve the stable LSP specification version and the initial client interoperability matrix.
4. Approve the first-release operating-system matrix or explicitly narrow it.
5. Approve the configuration authority and precedence expected from editor settings versus repository-owned configuration.
6. Decide whether external include roots may be read when explicitly configured and what user-visible authorization is required.
7. Approve or revise the 100,000-line scale target, latency thresholds, 500 MiB memory target, and reference hardware.
8. Decide whether macro-aware rename is required for first release or may remain unsupported until completeness can be demonstrated.
9. Approve the authoritative Z80 and assembler references used to build the conformance corpus.
10. Decide whether the initial release must ship an editor-specific installation package or only an editor-neutral server executable and setup documentation.

## Decisions Requested

- Approve, revise, or reject the first-release scope and priority assignments.
- Resolve Questions for CTO 1–10.
- Name the authoritative instruction-set and assembler-dialect references.
- Name at least one representative real project or corpus for acceptance testing.
- Approve the requirements for architecture review after those decisions are recorded.

## Recommended Next Step

Conduct architect review only after the CTO resolves the Pasmo version range, processor, protocol/client, configuration-authority, and acceptance-corpus decisions. Architecture should then translate these approved requirements into system boundaries and responsibilities without altering product scope.

## Approval Status

draft — Pasmo and canonical hexadecimal notation approved; version-dependent and other acceptance decisions remain unresolved

## Architect Review

Not yet performed.

## CTO Review

Pending. See **Questions for CTO** and **Decisions Requested**.

## Sign-Off

### Author

- Signer: Product Owner Agent
- Signer Type: agent
- Role: Requirements author
- Review Perspective: requirements drafting
- Disposition: submitted-for-review
- Summary Notes: Requirements revised to select Pasmo, make `$`-prefixed hexadecimal canonical, and require non-standard diagnostics for other Pasmo-valid hexadecimal forms. No architecture or implementation decisions approved. Pasmo version range, processor, protocol/client, configuration, and acceptance baselines still require human decisions.
- Date: 2026-09-06

### Review Entries

No review entries yet.

### CTO Sign-Off

- Signer: CTO (Human)
- Signer Type: human
- Status: pending

### Workflow Status

- Current Status: draft
