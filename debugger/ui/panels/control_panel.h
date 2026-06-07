//
// Z80 Digital Twin Debugger - Control panel
// Copyright (c) 2025-2026 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//

#ifndef Z80_DBG_CONTROL_PANEL_H
#define Z80_DBG_CONTROL_PANEL_H

#include "panel.h"

namespace z80::dbg {

/// @brief Execution controls (Step/Step Over/Run/Pause/Reset) and status line.
class ControlPanel : public Panel {
public:
    void Draw(UiContext& ctx) override;
};

} // namespace z80::dbg

#endif // Z80_DBG_CONTROL_PANEL_H
