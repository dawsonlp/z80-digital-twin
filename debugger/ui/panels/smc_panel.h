//
// Z80 Digital Twin Debugger - Self-modifying-code panel
// Copyright (c) 2025 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//

#ifndef Z80_DBG_SMC_PANEL_H
#define Z80_DBG_SMC_PANEL_H

#include "panel.h"

namespace z80::dbg {

/// @brief Self-modifying-code log: who wrote which code byte, old -> new, when.
///        Click a row to jump the disassembly to the target or the writer.
class SmcPanel : public Panel {
public:
    void Draw(UiContext& ctx) override;
};

} // namespace z80::dbg

#endif // Z80_DBG_SMC_PANEL_H
