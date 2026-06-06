//
// Z80 Digital Twin - ZX Spectrum beeper resampler
// Copyright (c) 2025 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//
// The beeper is a 1-bit speaker toggled by OUT 0xFE bit 4. Its sound is the
// sequence of level changes over time — the same T-cycle event timeline as the
// border/screen, but reconstructed into PCM instead of pixels.
//
// BeeperResampler turns a stream of level edges (absolute T-cycle + level) into
// signed-16 mono samples at the device rate. Each output sample integrates the
// speaker level over its T-cycle window (1 s = cpu_hz T = sample_rate samples),
// so a tone reproduces with proper averaging rather than crude point-sampling —
// "write the waveform at the times the level changed, mapped via the T-cycle."
// It works in absolute T-cycles, so there is no per-frame drift.
//

#ifndef Z80_MACHINE_SPECTRUM_BEEPER_H
#define Z80_MACHINE_SPECTRUM_BEEPER_H

#include <algorithm>
#include <cstdint>
#include <vector>

namespace z80::machine::spectrum {

class BeeperResampler {
public:
    BeeperResampler(uint32_t cpu_hz, uint32_t sample_rate, int16_t amplitude = 9000) noexcept
        : cpu_hz_(cpu_hz), rate_(sample_rate), amp_(amplitude) {}

    /// @brief The speaker changed to @p level (0/1) at absolute T-cycle @p cycle.
    ///        Emits any samples whose window closed before it.
    void edge(uint64_t cycle, int level, std::vector<int16_t>& out) {
        advance(cycle, out);
        level_ = level ? 1 : 0;
    }

    /// @brief Emit samples up to absolute T-cycle @p cycle at the current level.
    void advance(uint64_t cycle, std::vector<int16_t>& out) {
        while (now_ < cycle) {
            // Absolute T-cycle at which the current sample's window ends.
            const uint64_t boundary = ((samples_ + 1) * cpu_hz_) / rate_;
            const uint64_t step_to = std::min(cycle, boundary);
            high_ += static_cast<uint64_t>(level_) * (step_to - now_);
            now_ = step_to;
            if (now_ >= boundary) {
                const uint64_t window = boundary - window_start_;
                // Mean level over the window, mapped to a square centred on zero.
                const int64_t s = window
                    ? ((2 * static_cast<int64_t>(high_) - static_cast<int64_t>(window)) * amp_) /
                          static_cast<int64_t>(window)
                    : 0;
                out.push_back(static_cast<int16_t>(s));
                ++samples_;
                window_start_ = boundary;
                high_ = 0;
            }
        }
    }

    [[nodiscard]] uint64_t samples_emitted() const noexcept { return samples_; }

private:
    uint32_t cpu_hz_;
    uint32_t rate_;
    int16_t amp_;
    int level_ = 0;             ///< current speaker level (0/1)
    uint64_t now_ = 0;          ///< absolute T-cycle processed so far
    uint64_t window_start_ = 0; ///< absolute T-cycle of the current window's start
    uint64_t high_ = 0;         ///< accumulated "high" T-cycles in the current window
    uint64_t samples_ = 0;      ///< total samples emitted
};

} // namespace z80::machine::spectrum

#endif // Z80_MACHINE_SPECTRUM_BEEPER_H
