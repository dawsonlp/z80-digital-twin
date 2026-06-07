//
// Z80 Digital Twin Debugger - KeyboardPanel
// Copyright (c) 2025 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//
// A live view of the ZX Spectrum keyboard matrix: the 40 keys in their physical
// layout, lighting up as the ULA sees them pressed. Use it to see exactly which
// host keys/combos reach the matrix (and which the OS swallows).
//

#ifndef Z80_DBG_KEYBOARD_PANEL_H
#define Z80_DBG_KEYBOARD_PANEL_H

#include "panel.h"

namespace z80::machine::spectrum { class Ula; }

namespace z80::dbg {

class KeyboardPanel : public Panel {
public:
    explicit KeyboardPanel(z80::machine::spectrum::Ula& ula) : ula_(&ula) {}
    void Draw(UiContext& ctx) override;

private:
    z80::machine::spectrum::Ula* ula_;
};

} // namespace z80::dbg

#endif // Z80_DBG_KEYBOARD_PANEL_H
