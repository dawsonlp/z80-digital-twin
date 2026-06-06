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
//   * IN from an even port returns the keyboard matrix + EAR — the port's high
//     byte selects half-rows (active low); other ports float.
//   * end_frame() advances the FLASH phase (every 16 frames) and starts a fresh
//     border timeline carrying the last colour forward.
//   * As a FrameSource it answers border_for_line() from the resolved timeline
//     and screen_byte() beam-accurately: it observes writes to the display file
//     (0x4000..0x5AFF) with their frame T-state, and reconstructs each byte as of
//     the moment the beam fetched it for a given scanline — so per-scanline
//     attribute/bitmap changes (multicolour, raster splits) render correctly. A
//     byte not written this frame is read straight from RAM (its constant value).
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
#include <unordered_map>
#include <utility>
#include <vector>

namespace z80::machine::spectrum {

class Ula {
public:
    /// @brief A speaker level change: absolute T-cycle and the new level (0/1).
    struct BeeperEdge {
        uint64_t cycle;
        uint8_t level;
    };

    // -- Wiring (set once, after the CPU exists) -----------------------------
    void set_clock(std::function<uint64_t()> clock) { clock_ = std::move(clock); }
    void set_reader(std::function<uint8_t(uint16_t)> reader) { read_ = std::move(reader); }

    /// @brief Source of the EAR input bit (tape). true = high. Unset = idle high.
    void set_ear_source(std::function<bool()> ear) { ear_source_ = std::move(ear); }

    // -- Port handlers (wire into CallbackIo) --------------------------------

    /// @brief OUT handler. The ULA decodes even ports: bits 0..2 set the border,
    ///        bit 4 the speaker (beeper). Border changes are stamped frame-relative
    ///        (per-scanline reconstruction); beeper edges are stamped absolute
    ///        (the resampler works in absolute T-cycles).
    void write_port(uint16_t port, uint8_t value) {
        if ((port & 1) != 0) return;
        const uint8_t colour = static_cast<uint8_t>(value & 0x07);
        border_events_.push_back({frame_tstate(), colour});
        current_border_ = colour;

        const uint8_t speaker = (value >> 4) & 1u;
        if (speaker != beeper_level_) {
            beeper_edges_.push_back({clock_ ? clock_() : 0, speaker});
            beeper_level_ = speaker;
        }
    }

    /// @brief IN handler. Even ports read the keyboard matrix + EAR; other ports
    ///        float (0xFF). The port's high byte selects half-rows (active low):
    ///        A(8+r) low selects half-row r; the result is the AND of every
    ///        selected row, so a pressed key in any of them pulls its bit low.
    [[nodiscard]] uint8_t read_port(uint16_t port) const {
        if ((port & 1) != 0) return 0xFF;
        uint8_t result = 0x1F;                       // D0..D4 high = no key
        for (int row = 0; row < 8; ++row)
            if (((port >> (8 + row)) & 1) == 0)      // address line low -> row selected
                result &= key_rows_[static_cast<std::size_t>(row)];
        const bool ear = ear_source_ ? ear_source_() : true;   // D6 = EAR (idle high)
        return static_cast<uint8_t>((result & 0x1F) | 0xA0 | (ear ? 0x40 : 0x00));
    }

    // -- Display-file write observation (beam-accurate screen) ---------------

    /// @brief Memory-write observer (wire into ObservableMemory). Records writes
    ///        to the display file with their frame T-state; others are ignored.
    void on_write(uint16_t address, uint8_t old_value, uint8_t new_value) {
        if (address < kScreenStart || address > kScreenEnd) return;
        ScreenCell& cell = screen_writes_[address];
        if (cell.writes.empty()) cell.initial = old_value;   // value at frame start
        cell.writes.push_back({frame_tstate(), new_value});
    }

    /// @brief Start a frame: drop the previous frame's display-write history and
    ///        beeper edges (both consumed after end_frame, before the next begin).
    void begin_frame() {
        screen_writes_.clear();
        beeper_edges_.clear();
    }

    // -- Beeper (audio) ------------------------------------------------------

    /// @brief This frame's speaker edges (absolute T-cycle, level 0/1), in order.
    [[nodiscard]] const std::vector<BeeperEdge>& beeper_edges() const noexcept {
        return beeper_edges_;
    }
    [[nodiscard]] uint8_t beeper_level() const noexcept { return beeper_level_; }

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

    /// @brief The display byte at @p address as the beam saw it on @p display_line
    ///        (0..191): the last write at or before that line's fetch T-state, or
    ///        the frame-start value. Bytes untouched this frame read straight from
    ///        RAM (their value is constant across the frame).
    [[nodiscard]] uint8_t screen_byte(uint16_t address, int display_line) const {
        const auto it = screen_writes_.find(address);
        if (it == screen_writes_.end()) return read_ ? read_(address) : 0xFF;

        const uint32_t cutoff = static_cast<uint32_t>(timing::kDisplayStartT) +
                                static_cast<uint32_t>(display_line) *
                                static_cast<uint32_t>(timing::kTPerLine);
        uint8_t value = it->second.initial;
        for (const ScreenWrite& w : it->second.writes) {
            if (w.tstate <= cutoff) value = w.value;
            else break;                                  // writes are in time order
        }
        return value;
    }

    // -- Status --------------------------------------------------------------

    [[nodiscard]] bool flash_on() const { return screen::flash_phase(frame_counter_); }
    [[nodiscard]] uint64_t frame_counter() const noexcept { return frame_counter_; }
    [[nodiscard]] uint8_t border() const noexcept { return current_border_; }

    // -- Keyboard input (host -> matrix) -------------------------------------

    /// @brief Press a key: clear its data bit in the half-row (0 = pressed).
    void key_down(uint8_t half_row, uint8_t bit) noexcept {
        if (half_row < 8 && bit < 5)
            key_rows_[half_row] = static_cast<uint8_t>(key_rows_[half_row] & ~(1u << bit));
    }

    /// @brief Release a key: set its data bit back to 1.
    void key_up(uint8_t half_row, uint8_t bit) noexcept {
        if (half_row < 8 && bit < 5)
            key_rows_[half_row] = static_cast<uint8_t>(key_rows_[half_row] | (1u << bit));
    }

    /// @brief Release every key (call before re-applying the host key state).
    void release_all_keys() noexcept { key_rows_.fill(0x1F); }

private:
    // The display file: 6144 bytes of bitmap (0x4000..0x57FF) + 768 of attributes
    // (0x5800..0x5AFF).
    static constexpr uint16_t kScreenStart = 0x4000;
    static constexpr uint16_t kScreenEnd   = 0x5AFF;

    struct BorderEvent {
        uint32_t tstate;   ///< Frame-relative T-state of the change.
        uint8_t colour;    ///< Border colour 0..7.
    };

    struct ScreenWrite {
        uint32_t tstate;   ///< Frame-relative T-state of the write.
        uint8_t value;     ///< Byte written.
    };
    struct ScreenCell {
        uint8_t initial = 0;              ///< Value at frame start (old of the first write).
        std::vector<ScreenWrite> writes;  ///< This frame's writes, in time order.
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
    std::function<bool()> ear_source_;

    std::vector<BorderEvent> border_events_{{0, 0}};   // always non-empty (baseline)
    std::unordered_map<uint16_t, ScreenCell> screen_writes_;  // display-file writes this frame
    std::vector<BeeperEdge> beeper_edges_;                    // speaker edges this frame
    std::array<uint8_t, video::kFrameHeight> border_per_line_{};
    uint8_t current_border_ = 0;
    uint8_t beeper_level_ = 0;
    std::array<uint8_t, 8> key_rows_{0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F};
    uint64_t frame_start_ = 0;
    uint64_t frame_counter_ = 0;
};

} // namespace z80::machine::spectrum

#endif // Z80_MACHINE_SPECTRUM_ULA_H
