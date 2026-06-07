//
// Z80 Digital Twin Debugger - SpectrumScreenPanel
// Copyright (c) 2025-2026 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//
// Shows the live ZX Spectrum picture (border + display) as a GL texture, drawn
// from the ULA acting as the renderer's FrameSource. Present only when the
// debugger is driving a Spectrum (z80_debugger --spectrum <rom>).
//

#ifndef Z80_DBG_SCREEN_PANEL_H
#define Z80_DBG_SCREEN_PANEL_H

#include "panel.h"

namespace z80::machine::spectrum { class Ula; }

namespace z80::dbg {

class SpectrumScreenPanel : public Panel {
public:
    explicit SpectrumScreenPanel(z80::machine::spectrum::Ula& ula) : ula_(&ula) {}
    void Draw(UiContext& ctx) override;

private:
    z80::machine::spectrum::Ula* ula_;
    unsigned int texture_ = 0;   ///< GL texture id (lazily created)
};

} // namespace z80::dbg

#endif // Z80_DBG_SCREEN_PANEL_H
