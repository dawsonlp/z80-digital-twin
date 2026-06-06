//
// Z80 Digital Twin Debugger - Disassembly panel
// Copyright (c) 2025 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//

#ifndef Z80_DBG_DISASSEMBLY_PANEL_H
#define Z80_DBG_DISASSEMBLY_PANEL_H

#include "panel.h"
#include "symbol_edit.h"

#include <cstdint>
#include <vector>

namespace z80::dbg {

/// @brief Disassembly view: follow-PC, current-instruction highlight, clickable
///        breakpoint gutter, inline symbol labels, right-click labeling,
///        go-to (hex or symbol) with back/forward history.
class DisassemblyPanel : public Panel {
public:
    void Draw(UiContext& ctx) override;

private:
    bool follow_pc_ = true;
    uint16_t top_ = 0x0000;
    char goto_buf_[64] = "";
    std::vector<uint16_t> back_;      ///< addresses navigated away from
    std::vector<uint16_t> forward_;   ///< addresses undone with Back
    SymbolEditState edit_;
};

} // namespace z80::dbg

#endif // Z80_DBG_DISASSEMBLY_PANEL_H
