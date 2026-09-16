# C++ project conventions

**Status:** agreed direction for incremental work, 2026-09-15. These rules do not
claim that all existing code has already been audited or converted.

Use the configured C++23 baseline. Keep changes focused on the feature or defect
being addressed; do not bundle broad modernization with symbol work.

- Make ownership and lifetime explicit. Prefer values and RAII; use owning smart
  pointers when ownership is dynamic. References, pointers, views and callbacks
  must have a clear lifetime contract. Observer registration must also be safe
  if construction fails partway through.
- Preserve invariants across failures. Validate conflicts and prepare potentially
  throwing work before committing mutations. Multiple indexes must describe the
  same state after success and after failure.
- For new interfaces, use domain types for identities, `std::optional` for absence,
  `std::variant` for mutually exclusive bindings, and `std::expected` for ordinary
  recoverable failures. Avoid boolean combinations that admit invalid states.
  Preserve existing API conventions when making bounded repairs; document their
  error contract. Stage 0 retains the void symbol mutation API with exceptions;
  the planned project authority should expose typed errors.
- Use standard containers and strings. Prefer spans/views for bounded borrowing,
  while checking their lifetimes. Avoid fixed buffers that silently truncate
  user data. Use explicit conversions and checked arithmetic at address/range
  boundaries; distinguish Z80 wrapping semantics from host validation arithmetic.
- Use `const`, `[[nodiscard]]`, and `noexcept` where their contracts are meaningful.
  Do not mark allocating or otherwise throwing operations `noexcept` by habit.
- Test observable behavior and failure boundaries. Use compiler warnings and the
  existing test suite; document skipped tests and configuration limits. A passing
  test is evidence for that configuration, not universal portability proof.

## Deliberate register-union exception

Retain the project's union-based register-pair representation. The owner explicitly
prefers its direct byte/word access and has excluded replacing it from modernization
work. Cross-member reads are a deliberate implementation dependency, not a claim
that arbitrary union type-punning is portable ISO C++.

Keep this exception localized to the established register representation. Document
byte-order/layout assumptions there and validate register behavior on supported
compiler/target configurations when changing that boundary. Do not generalize the
exception into unchecked object-lifetime or aliasing practices elsewhere. Broader
portability changes require a separate architectural decision.

The [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines)
are useful guidance; the explicit project exception above remains authoritative
for this codebase.
