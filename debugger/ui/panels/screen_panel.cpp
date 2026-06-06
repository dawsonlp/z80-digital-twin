//
// Z80 Digital Twin Debugger - SpectrumScreenPanel implementation
// Copyright (c) 2025 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//

#include "screen_panel.h"
#include "ui_context.h"

#include "spectrum/ula.h"
#include "spectrum/video.h"
#include "spectrum/screen.h"

#define GL_SILENCE_DEPRECATION
#include "imgui.h"
#include <GLFW/glfw3.h>

#include <array>
#include <cstdint>

namespace z80::dbg {

namespace {
namespace v = z80::machine::spectrum::video;
namespace s = z80::machine::spectrum::screen;
}

void SpectrumScreenPanel::Draw(UiContext& /*ctx*/) {
    z80::machine::spectrum::Ula& ula = *ula_;

    // Render the current frame to palette indices, then to RGBA8888.
    std::array<uint8_t, v::kFramePixels> indices{};
    v::render_frame(ula, ula.flash_on(), indices);

    std::array<uint32_t, v::kFramePixels> rgba{};
    for (std::size_t i = 0; i < indices.size(); ++i) {
        const s::Rgb c = s::to_rgb(indices[i]);
        rgba[i] = 0xFF000000u | (static_cast<uint32_t>(c.b) << 16) |
                  (static_cast<uint32_t>(c.g) << 8) | static_cast<uint32_t>(c.r);
    }

    if (texture_ == 0) {
        glGenTextures(1, &texture_);
        glBindTexture(GL_TEXTURE_2D, texture_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, v::kFrameWidth, v::kFrameHeight,
                     0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    }
    glBindTexture(GL_TEXTURE_2D, texture_);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, v::kFrameWidth, v::kFrameHeight,
                    GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());

    ImGui::Begin("Spectrum Screen");
    ImGui::Image(reinterpret_cast<ImTextureID>(static_cast<intptr_t>(texture_)),
                 ImVec2(v::kFrameWidth * 2.0f, v::kFrameHeight * 2.0f));
    ImGui::TextDisabled("type here when focused (Shift=CAPS, Ctrl=SYM, Bksp=DELETE)");
    ImGui::End();
}

} // namespace z80::dbg
