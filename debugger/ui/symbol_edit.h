// Shared analysis-backed symbol editor. Copyright (c) 2026 Larry Dawson. MIT.
#pragma once
#include "analysis_workspace.h"

namespace z80::dbg {
struct UiContext;
struct SymbolEditState {
    uint16_t address = 0;
    std::string name, summary;
    uint32_t extent = 0; // Zero means unknown in the form, never a stored zero extent.
    int type_index = 0;
    std::optional<Symbol> original;
    std::optional<analysis::SymbolId> id;
    std::string error;
    bool ambiguous = false;
};
void PrimeSymbolEdit(SymbolEditState &state, uint16_t address, const analysis::Workspace &workspace);
void DrawSymbolEditForm(UiContext &context, SymbolEditState &state);
} // namespace z80::dbg
