//
// Z80 Digital Twin Debugger - Disassembly panel
// Copyright (c) 2025 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//

#ifndef Z80_DBG_DISASSEMBLY_PANEL_H
#define Z80_DBG_DISASSEMBLY_PANEL_H

#include "panel.h"

#include <cstdint>

namespace z80::dbg {

/// @brief Disassembly view: follow-PC, current-instruction highlight, clickable
///        breakpoint gutter, and inline symbol labels.
class DisassemblyPanel : public Panel {
public:
    void Draw(UiContext& ctx) override;

private:
    bool follow_pc_ = true;
    uint16_t top_ = 0x0000;
};

} // namespace z80::dbg

#endif // Z80_DBG_DISASSEMBLY_PANEL_H
