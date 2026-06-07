//
// Z80 Digital Twin Debugger - Registers panel
// Copyright (c) 2025-2026 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//

#ifndef Z80_DBG_REGISTERS_PANEL_H
#define Z80_DBG_REGISTERS_PANEL_H

#include "panel.h"

namespace z80::dbg {

/// @brief All Z80 registers (editable when paused), flags, and IM/IFF state.
class RegistersPanel : public Panel {
public:
    void Draw(UiContext& ctx) override;
};

} // namespace z80::dbg

#endif // Z80_DBG_REGISTERS_PANEL_H
