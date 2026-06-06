//
// Z80 Digital Twin Debugger - Symbol styling helpers (shared)
// Copyright (c) 2025 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//
// Colour-per-type and a hover tooltip, used by any panel that renders symbols.
//

#ifndef Z80_DBG_SYMBOL_STYLE_H
#define Z80_DBG_SYMBOL_STYLE_H

#include "symbol_table.h"
#include "imgui.h"

namespace z80::dbg {

/// @brief Display colour for a symbol type.
ImVec4 SymbolColor(SymbolType type);

/// @brief Code symbols get their own line label; data symbols only show inline.
bool IsCodeLabel(SymbolType type);

/// @brief If the last-drawn item is hovered, show a tooltip with the symbol's
///        name, type, address, size, and description.
void SymbolTooltipIfHovered(const Symbol& sym);

} // namespace z80::dbg

#endif // Z80_DBG_SYMBOL_STYLE_H
