//
// Z80 Digital Twin - ZX Spectrum ULA
// Copyright (c) 2025 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//
// The ULA, modelled as a clock-aware peripheral that doubles as the renderer's
// FrameSource:
//   * OUT to an even port (0xFE) latches the border colour (bits 0..2). Each
//     change is timestamped with its frame-relative T-state, so per-scanline
//     border effects (loading stripes, rainbows) reconstruct correctly. The
//     timeline is resolved to a per-line colour once per frame.
//   * IN from an even port returns the keyboard half-rows + EAR (currently
//     "no keys"); other ports float.
//   * end_frame() advances the FLASH phase (every 16 frames) and starts a fresh
//     border timeline carrying the last colour forward.
//   * As a FrameSource it answers border_for_line() from the resolved timeline
//     and screen_byte() from RAM via the installed reader (the simple
//     final-memory source — correct for static/boot screens and rainbow borders).
//
// The ULA learns the current T-state through an installed clock callback (it is
// the clock master in real hardware) and reads RAM through a reader callback, so
// it stays independent of the CPU's template configuration.
//

#ifndef Z80_MACHINE_SPECTRUM_ULA_H
#define Z80_MACHINE_SPECTRUM_ULA_H

#include "screen.h"
#include "video.h"
#include "timing.h"

#include <array>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace z80::machine::spectrum {

class Ula {
public:
    // -- Wiring (set once, after the CPU exists) -----------------------------
    void set_clock(std::function<uint64_t()> clock) { clock_ = std::move(clock); }
    void set_reader(std::function<uint8_t(uint16_t)> reader) { read_ = std::move(reader); }

    // -- Port handlers (wire into CallbackIo) --------------------------------

    /// @brief OUT handler. The ULA decodes even ports; bits 0..2 set the border.
    void write_port(uint16_t port, uint8_t value) {
        if ((port & 1) != 0) return;
        const uint8_t colour = static_cast<uint8_t>(value & 0x07);
        border_events_.push_back({frame_tstate(), colour});
        current_border_ = colour;
    }

    /// @brief IN handler. Even ports read the keyboard half-rows + EAR; the
    ///        keyboard is currently "all up". Other ports float (0xFF).
    [[nodiscard]] uint8_t read_port(uint16_t port) const {
        if ((port & 1) != 0) return 0xFF;
        return static_cast<uint8_t>(keyboard_ | 0xE0);   // bits 5..7 high (EAR high)
    }

    // -- Frame advance -------------------------------------------------------

    /// @brief Resolve the border timeline to a per-line colour, advance the
    ///        FLASH phase, and begin a fresh timeline for the next frame.
    void end_frame() {
        resolve_border();
        const uint8_t carry = current_border_;
        border_events_.clear();
        border_events_.push_back({0, carry});   // baseline for the new frame
        ++frame_counter_;
        if (clock_) frame_start_ = clock_();
    }

    // -- FrameSource (for video::render_frame) -------------------------------

    [[nodiscard]] uint8_t border_for_line(int rendered_line) const {
        if (rendered_line < 0) rendered_line = 0;
        if (rendered_line >= video::kFrameHeight) rendered_line = video::kFrameHeight - 1;
        return border_per_line_[static_cast<std::size_t>(rendered_line)];
    }

    [[nodiscard]] uint8_t screen_byte(uint16_t address, int /*display_line*/) const {
        return read_ ? read_(address) : 0xFF;
    }

    // -- Status --------------------------------------------------------------

    [[nodiscard]] bool flash_on() const { return screen::flash_phase(frame_counter_); }
    [[nodiscard]] uint64_t frame_counter() const noexcept { return frame_counter_; }
    [[nodiscard]] uint8_t border() const noexcept { return current_border_; }

    /// @brief Set keyboard half-row bits (bits 0..4; 0 = pressed). Full matrix
    ///        decode by address half lands with input handling.
    void set_keyboard(uint8_t bits) noexcept { keyboard_ = static_cast<uint8_t>(bits & 0x1F); }

private:
    struct BorderEvent {
        uint32_t tstate;   ///< Frame-relative T-state of the change.
        uint8_t colour;    ///< Border colour 0..7.
    };

    [[nodiscard]] uint32_t frame_tstate() const {
        if (!clock_) return 0;
        const uint64_t now = clock_();
        return static_cast<uint32_t>(now >= frame_start_ ? now - frame_start_ : 0);
    }

    /// @brief Map each rendered line to the border colour active when the beam
    ///        drew it (sampled at that line's start T-state).
    void resolve_border() {
        for (int r = 0; r < video::kFrameHeight; ++r) {
            const int abs_line = r + (64 - video::kBorderTop);   // rendered row -> Spectrum scanline
            const uint32_t sample = static_cast<uint32_t>(abs_line) *
                                    static_cast<uint32_t>(timing::kTPerLine);
            uint8_t colour = border_events_.front().colour;
            for (const BorderEvent& e : border_events_) {
                if (e.tstate <= sample) colour = e.colour;
                else break;                                       // events are in time order
            }
            border_per_line_[static_cast<std::size_t>(r)] = colour;
        }
    }

    std::function<uint64_t()> clock_;
    std::function<uint8_t(uint16_t)> read_;

    std::vector<BorderEvent> border_events_{{0, 0}};   // always non-empty (baseline)
    std::array<uint8_t, video::kFrameHeight> border_per_line_{};
    uint8_t current_border_ = 0;
    uint8_t keyboard_ = 0x1F;          // bits 0..4 = 1 -> no keys pressed
    uint64_t frame_start_ = 0;
    uint64_t frame_counter_ = 0;
};

} // namespace z80::machine::spectrum

#endif // Z80_MACHINE_SPECTRUM_ULA_H
