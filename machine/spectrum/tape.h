//
// Z80 Digital Twin - ZX Spectrum tape (.tap / .tzx, real-signal)
// Copyright (c) 2025-2026 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//
// Loads a tape image and synthesises the *actual* cassette signal the ROM's
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
// `.tzx` format: a 10-byte header ("ZXTape!" + 0x1A + major + minor) then a
// stream of typed blocks. We synthesise the data-bearing blocks (standard 0x10,
// turbo 0x11, pure tone 0x12, pulse sequence 0x13, pure data 0x14, pause 0x20)
// through the same parameterised emitter, and skip the metadata blocks
// (text/archive/hardware/group/…) by their declared length. The standard block
// is just a `.tap` block wrapped with an explicit pause, so both paths share
// `add_block`.
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
    static constexpr uint32_t kTPerMs      = 3500;   // 3.5 MHz CPU -> T per ms
    static constexpr uint32_t kPauseMs     = 1000;   // gap after a .tap block

    /// @brief Parse a tape image, auto-detecting `.tzx` (by its "ZXTape!" magic)
    ///        vs `.tap`. Returns false if nothing playable was produced.
    bool load(std::span<const uint8_t> data) {
        if (is_tzx(data)) return load_tzx(data);
        return load_tap(data);
    }

    /// @brief Parse a `.tap` image into the pulse train. Returns false if empty
    ///        or malformed (nothing playable).
    bool load_tap(std::span<const uint8_t> data) {
        reset_pulses();
        std::size_t i = 0;
        while (i + 2 <= data.size()) {
            const uint16_t len = static_cast<uint16_t>(data[i] | (data[i + 1] << 8));
            i += 2;
            if (len == 0 || i + len > data.size()) break;   // truncated / invalid
            add_standard_block(data.subspan(i, len), kPauseMs);
            i += len;
            ++blocks_;
        }
        stop();
        return !pulses_.empty();
    }

    /// @brief Parse a `.tzx` image into the pulse train. Handles the data-bearing
    ///        block types and skips known metadata; bails (keeping what's parsed)
    ///        on an unrecognised block whose length we can't determine.
    bool load_tzx(std::span<const uint8_t> data) {
        reset_pulses();
        if (!is_tzx(data)) return false;

        const auto rd16 = [&](std::size_t off) -> uint32_t {
            return (off + 1 < data.size())
                       ? static_cast<uint32_t>(data[off] | (data[off + 1] << 8))
                       : 0;
        };
        const auto rd24 = [&](std::size_t off) -> uint32_t {
            return (off + 2 < data.size())
                       ? static_cast<uint32_t>(data[off] | (data[off + 1] << 8) |
                                               (data[off + 2] << 16))
                       : 0;
        };
        const auto byte = [&](std::size_t off) -> uint8_t {
            return off < data.size() ? data[off] : 0;
        };

        std::size_t i = 10;   // skip the "ZXTape!" header
        while (i < data.size()) {
            const uint8_t id = data[i++];
            switch (id) {
            case 0x10: {   // Standard speed data: [pause:2][len:2][data]
                const uint32_t pause = rd16(i);
                const uint32_t len   = rd16(i + 2);
                i += 4;
                if (i + len > data.size()) return done();
                add_standard_block(data.subspan(i, len), pause);
                i += len;
                ++blocks_;
                break;
            }
            case 0x11: {   // Turbo speed data: fully parameterised
                const uint32_t pilot = rd16(i);
                const uint32_t s1    = rd16(i + 2);
                const uint32_t s2    = rd16(i + 4);
                const uint32_t zero  = rd16(i + 6);
                const uint32_t one   = rd16(i + 8);
                const uint32_t pcnt  = rd16(i + 10);
                const uint8_t  used  = byte(i + 12);
                const uint32_t pause = rd16(i + 13);
                const uint32_t len   = rd24(i + 15);
                i += 18;
                if (i + len > data.size()) return done();
                add_block(data.subspan(i, len), pilot, static_cast<int>(pcnt),
                          s1, s2, zero, one, used, pause);
                i += len;
                ++blocks_;
                break;
            }
            case 0x12: {   // Pure tone: [pulse:2][count:2]
                const uint32_t pulse = rd16(i);
                const uint32_t count = rd16(i + 2);
                i += 4;
                for (uint32_t p = 0; p < count; ++p) push(pulse);
                break;
            }
            case 0x13: {   // Pulse sequence: [n:1][pulse:2 * n]
                const uint8_t n = byte(i);
                i += 1;
                for (uint8_t p = 0; p < n; ++p) { push(rd16(i)); i += 2; }
                break;
            }
            case 0x14: {   // Pure data: [zero:2][one:2][used:1][pause:2][len:3][data]
                const uint32_t zero  = rd16(i);
                const uint32_t one   = rd16(i + 2);
                const uint8_t  used  = byte(i + 4);
                const uint32_t pause = rd16(i + 5);
                const uint32_t len   = rd24(i + 7);
                i += 10;
                if (i + len > data.size()) return done();
                add_block(data.subspan(i, len), 0, 0, 0, 0, zero, one, used, pause);
                i += len;
                ++blocks_;
                break;
            }
            case 0x20:     // Pause / stop tape: [pause:2 ms] (0 = stop the tape)
                push_pause_ms(rd16(i));
                i += 2;
                break;

            // -- Metadata blocks: no signal, skip by declared length ----------
            case 0x21: i += 1 + byte(i); break;             // group start [n][name]
            case 0x22: break;                               // group end (no body)
            case 0x30: i += 1 + byte(i); break;             // text desc [n][text]
            case 0x31: i += 2 + byte(i + 1); break;         // message [time][n][text]
            case 0x32: i += 2 + rd16(i); break;             // archive info [len:2][..]
            case 0x33: i += 1 + 3 * byte(i); break;         // hardware [n][n*3]
            case 0x35: i += 0x14 + rd24(i + 0x10); break;   // custom info [id:16][len:4][..]
            case 0x5A: i += 9; break;                       // glue block

            default:
                // Unknown block: we can't know its length, so stop here and keep
                // whatever we've already synthesised.
                return done();
            }
        }
        return done();
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

    /// @brief True once playback has run past the final pulse (load finished).
    [[nodiscard]] bool finished(uint64_t cpu_cycle) const noexcept {
        if (!playing_ || pulses_.empty()) return false;
        const uint64_t elapsed = cpu_cycle >= start_cycle_ ? cpu_cycle - start_cycle_ : 0;
        return elapsed >= total_;
    }

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
    [[nodiscard]] static bool is_tzx(std::span<const uint8_t> d) noexcept {
        static constexpr char kMagic[] = "ZXTape!";
        if (d.size() < 10 || d[7] != 0x1A) return false;
        for (int k = 0; k < 7; ++k)
            if (d[k] != static_cast<uint8_t>(kMagic[k])) return false;
        return true;
    }

    void reset_pulses() {
        pulses_.clear();
        total_ = 0;
        blocks_ = 0;
    }

    bool done() {
        stop();
        return !pulses_.empty();
    }

    /// @brief Emit a standard ROM block: pilot length keyed off the flag byte,
    ///        standard sync/bit timings, all 8 bits used, then @p pause_ms gap.
    void add_standard_block(std::span<const uint8_t> block, uint32_t pause_ms) {
        const uint8_t flag = block.empty() ? 0 : block[0];
        const int pilot = (flag < 0x80) ? kPilotHeader : kPilotData;
        add_block(block, kPilotPulse, pilot, kSync1, kSync2,
                  kZeroPulse, kOnePulse, 8, pause_ms);
    }

    /// @brief The general pulse-train emitter shared by every data block.
    /// @param pilot_count  Number of pilot pulses (0 = no pilot tone, e.g. pure data).
    /// @param sync1,sync2  Sync pulses (0 = omit, e.g. pure data).
    /// @param used_bits    Bits used in the *last* byte (1–8); earlier bytes use 8.
    /// @param pause_ms     Trailing silence in milliseconds (0 = none).
    void add_block(std::span<const uint8_t> data, uint32_t pilot_pulse, int pilot_count,
                   uint32_t sync1, uint32_t sync2, uint32_t zero, uint32_t one,
                   uint8_t used_bits, uint32_t pause_ms) {
        for (int p = 0; p < pilot_count; ++p) push(pilot_pulse);
        if (sync1) push(sync1);
        if (sync2) push(sync2);
        for (std::size_t b = 0; b < data.size(); ++b) {
            const bool last = (b + 1 == data.size());
            const int bits = last ? (used_bits ? used_bits : 8) : 8;
            const uint8_t value = data[b];
            for (int k = 0; k < bits; ++k) {
                const uint32_t d = (value & (0x80u >> k)) ? one : zero;
                push(d);
                push(d);
            }
        }
        push_pause_ms(pause_ms);
    }

    void push_pause_ms(uint32_t ms) {
        if (ms) push(ms * kTPerMs);
    }

    void push(uint32_t tstates) {
        if (tstates == 0) return;
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
