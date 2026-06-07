//
// Z80 Digital Twin Debugger - Symbol edit popup (shared)
// Copyright (c) 2025-2026 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//
// A small reusable form for creating/editing/removing a symbol at an address.
// Panels own a SymbolEditState and render the form inside their own
// right-click context popup, so the labeling UX is identical everywhere.
//

#ifndef Z80_DBG_SYMBOL_EDIT_H
#define Z80_DBG_SYMBOL_EDIT_H

#include <cstdint>

namespace z80::dbg {

struct UiContext;
class SymbolTable;

/// @brief Persistent buffers for an open symbol-edit popup.
struct SymbolEditState {
    uint16_t address = 0;
    char name[64] = "";
    int type_index = 0;   ///< index into the form's type list
};

/// @brief Prime the buffers from any existing symbol at @p address (call when
///        the popup first appears).
void PrimeSymbolEdit(SymbolEditState& st, uint16_t address, const SymbolTable& symbols);

/// @brief Render the edit form inside an already-open popup. Applies Define /
///        Remove and closes the popup on action.
void DrawSymbolEditForm(UiContext& ctx, SymbolEditState& st);

} // namespace z80::dbg

#endif // Z80_DBG_SYMBOL_EDIT_H
