//
// Z80 Digital Twin - ZX Spectrum tape (.tap, real-signal)
// Copyright (c) 2025 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//
// Loads a `.tap` image and synthesises the *actual* cassette signal the ROM's
// LOAD routine expects, as a list of pulse durations (T-states). Playback maps
// the CPU's T-cycle to a pulse index and yields the EAR level (port 0xFE bit 6),
// so the ROM times the edges and decodes — the authentic load, with the loading
// stripes appearing for free via the border timeline. (Mirror of the sound path:
// here a T-cycle timeline *drives* IN instead of being recorded from OUT.)
//
// `.tap` format: a sequence of blocks `[len:2 LE][data: len]`; data[0] is the
// flag (0x00 header / 0xFF data). Standard ROM encoding per block: a pilot tone
// (pulses of 2168T — 8063 for a header, 3223 for data), two sync pulses
// (667T, 735T), then each byte MSB-first as two pulses per bit (855T for 0,
// 1710T for 1), then a pause. The level toggles on every pulse boundary.
//

#ifndef Z80_MACHINE_SPECTRUM_TAPE_H
#define Z80_MACHINE_SPECTRUM_TAPE_H

#include <cstdint>
#include <span>
#include <vector>

namespace z80::machine::spectrum {

class Tape {
public:
    // Standard ROM timing (T-states).
    static constexpr uint32_t kPilotPulse  = 2168;
    static constexpr uint32_t kSync1       = 667;
    static constexpr uint32_t kSync2       = 735;
    static constexpr uint32_t kZeroPulse   = 855;
    static constexpr uint32_t kOnePulse    = 1710;
    static constexpr int      kPilotHeader = 8063;   // pulses, flag < 0x80
    static constexpr int      kPilotData   = 3223;   // pulses, flag >= 0x80
    static constexpr uint32_t kPause       = 3'500'000;  // ~1 s gap after a block

    /// @brief Parse a `.tap` image into the pulse train. Returns false if empty
    ///        or malformed (nothing playable).
    bool load_tap(std::span<const uint8_t> data) {
        pulses_.clear();
        blocks_ = 0;
        std::size_t i = 0;
        while (i + 2 <= data.size()) {
            const uint16_t len = static_cast<uint16_t>(data[i] | (data[i + 1] << 8));
            i += 2;
            if (len == 0 || i + len > data.size()) break;   // truncated / invalid
            add_block(data.subspan(i, len));
            i += len;
            ++blocks_;
        }
        stop();
        return !pulses_.empty();
    }

    /// @brief Start playback as of @p cpu_cycle.
    void play(uint64_t cpu_cycle) noexcept {
        start_cycle_ = cpu_cycle;
        playing_ = true;
        cursor_index_ = 0;
        cursor_tstate_ = 0;
    }
    void stop() noexcept { playing_ = false; }

    [[nodiscard]] bool playing() const noexcept { return playing_; }
    [[nodiscard]] bool empty() const noexcept { return pulses_.empty(); }
    [[nodiscard]] std::size_t block_count() const noexcept { return blocks_; }
    [[nodiscard]] std::size_t pulse_count() const noexcept { return pulses_.size(); }
    [[nodiscard]] uint64_t total_tstates() const noexcept { return total_; }

    /// @brief The EAR level (true = high) at @p cpu_cycle. Idle high when not
    ///        playing or past the end (matches a disconnected EAR socket).
    [[nodiscard]] bool ear_level(uint64_t cpu_cycle) const noexcept {
        if (!playing_ || pulses_.empty()) return true;

        const uint64_t elapsed = cpu_cycle >= start_cycle_ ? cpu_cycle - start_cycle_ : 0;
        if (elapsed < cursor_tstate_) { cursor_index_ = 0; cursor_tstate_ = 0; }  // seek back
        while (cursor_index_ < pulses_.size() &&
               cursor_tstate_ + pulses_[cursor_index_] <= elapsed) {
            cursor_tstate_ += pulses_[cursor_index_];
            ++cursor_index_;
        }
        if (cursor_index_ >= pulses_.size()) return true;   // tape ended
        return initial_level_ ^ ((cursor_index_ & 1u) != 0);
    }

private:
    void add_block(std::span<const uint8_t> block) {
        const uint8_t flag = block[0];
        const int pilot = (flag < 0x80) ? kPilotHeader : kPilotData;
        for (int p = 0; p < pilot; ++p) push(kPilotPulse);
        push(kSync1);
        push(kSync2);
        for (uint8_t byte : block) {
            for (int bit = 7; bit >= 0; --bit) {
                const uint32_t d = (byte & (1u << bit)) ? kOnePulse : kZeroPulse;
                push(d);
                push(d);
            }
        }
        push(kPause);
    }

    void push(uint32_t tstates) {
        pulses_.push_back(tstates);
        total_ += tstates;
    }

    std::vector<uint32_t> pulses_;
    uint64_t total_ = 0;
    std::size_t blocks_ = 0;
    bool initial_level_ = false;   // signal starts low; toggles each pulse
    bool playing_ = false;
    uint64_t start_cycle_ = 0;

    // Forward-walk cache (playback advances monotonically during a load).
    mutable std::size_t cursor_index_ = 0;
    mutable uint64_t cursor_tstate_ = 0;
};

} // namespace z80::machine::spectrum

#endif // Z80_MACHINE_SPECTRUM_TAPE_H
