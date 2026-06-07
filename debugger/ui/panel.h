//
// Z80 Digital Twin Debugger - Panel interface
// Copyright (c) 2025-2026 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//
// A panel is a self-contained piece of the debugger UI. It owns its own view
// state and renders itself each frame from a shared UiContext. DebuggerApp owns
// a list of panels and draws them uniformly, so adding a new panel is just a new
// class plus one registration line.
//

#ifndef Z80_DBG_PANEL_H
#define Z80_DBG_PANEL_H

namespace z80::dbg {

struct UiContext;

class Panel {
public:
    virtual ~Panel() = default;

    /// @brief Render this panel for the current frame.
    virtual void Draw(UiContext& ctx) = 0;
};

} // namespace z80::dbg

#endif // Z80_DBG_PANEL_H
