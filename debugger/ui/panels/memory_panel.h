//
// Z80 Digital Twin Debugger - Memory panel
// Copyright (c) 2025 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//

#ifndef Z80_DBG_MEMORY_PANEL_H
#define Z80_DBG_MEMORY_PANEL_H

#include "panel.h"
#include "symbol_edit.h"

#include <cstdint>

namespace z80::dbg {

/// @brief Hex/ASCII memory dump with jump-to-address, changed-cell highlight,
///        and right-click address labeling.
class MemoryPanel : public Panel {
public:
    void Draw(UiContext& ctx) override;

private:
    char goto_buf_[8] = "0000";
    bool goto_pending_ = false;
    uint16_t goto_addr_ = 0x0000;
    SymbolEditState edit_;
};

} // namespace z80::dbg

#endif // Z80_DBG_MEMORY_PANEL_H
