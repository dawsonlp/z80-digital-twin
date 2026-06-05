//
// Z80 Digital Twin Debugger - I/O ports panel
// Copyright (c) 2025 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//

#ifndef Z80_DBG_IO_PANEL_H
#define Z80_DBG_IO_PANEL_H

#include "panel.h"

namespace z80::dbg {

/// @brief All 256 I/O ports with current value (hex/decimal).
class IoPanel : public Panel {
public:
    void Draw(UiContext& ctx) override;
};

} // namespace z80::dbg

#endif // Z80_DBG_IO_PANEL_H
