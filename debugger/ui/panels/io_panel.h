//
// Z80 Digital Twin Debugger - I/O ports panel
// Copyright (c) 2025-2026 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//

#ifndef Z80_DBG_IO_PANEL_H
#define Z80_DBG_IO_PANEL_H

#include "panel.h"

namespace z80::dbg {

/// @brief Passive bus-transaction log (OUT/IN). Recording is off by default — a
///        running machine (e.g. the Spectrum scanning the keyboard every frame)
///        would otherwise flood it — and is toggled on demand.
class IoPanel : public Panel {
public:
    void Draw(UiContext& ctx) override;

private:
    bool record_ = false;   ///< quiet by default; tick to capture bus activity
};

} // namespace z80::dbg

#endif // Z80_DBG_IO_PANEL_H
