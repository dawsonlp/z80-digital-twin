//
// Z80 Digital Twin - ZX Spectrum 48K machine
// Copyright (c) 2025 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//
// Ties the CPU, the ULA, and the generic frame clock into a running 48K:
//   * CPU = CPUImpl<FastMemory, CallbackIo> (the ULA wires its port decode into
//     the CallbackIo handlers; FastMemory is the flat 64 KB space — 16 KB ROM at
//     0x0000, 48 KB RAM above);
//   * the ULA is given the CPU's clock and a RAM reader, and acts as the
//     renderer's FrameSource;
//   * Machine<Cpu> runs each PAL frame (asserting the 50 Hz interrupt) and the
//     ULA advances at frame end.
//
// run_frame() advances one frame; render_indices()/render_rgba() produce the
// current picture. Headless-friendly: no UI or GL dependency here.
//

#ifndef Z80_MACHINE_SPECTRUM_SPECTRUM_MACHINE_H
#define Z80_MACHINE_SPECTRUM_SPECTRUM_MACHINE_H

#include "z80_cpu.h"
#include "io/callback_io.h"
#include "machine.h"
#include "screen.h"
#include "timing.h"
#include "ula.h"
#include "video.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace z80::machine::spectrum {

using SpectrumCpu = z80::CPUImpl<z80::FastMemory, z80::CallbackIo>;

class SpectrumMachine {
public:
    static constexpr int kWidth  = video::kFrameWidth;
    static constexpr int kHeight = video::kFrameHeight;
    static constexpr int kPixels = video::kFramePixels;

    SpectrumMachine() : machine_(cpu_, timing::kTPerFrame) {
        ula_.set_clock([this] { return cpu_.GetCycleCount(); });
        ula_.set_reader([this](uint16_t addr) { return cpu_.ReadMemory(addr); });
        cpu_.GetIo().OnOut([this](uint16_t port, uint8_t value) { ula_.write_port(port, value); });
        cpu_.GetIo().OnIn([this](uint16_t port) { return ula_.read_port(port); });
        cpu_.Reset();
    }

    /// @brief Load a ROM image (≤16 KB) at 0x0000 and reset the CPU.
    bool load_rom(std::span<const uint8_t> rom) {
        if (rom.empty() || rom.size() > 0x4000) return false;
        cpu_.LoadProgram(std::vector<uint8_t>(rom.begin(), rom.end()), 0x0000);
        cpu_.Reset();
        return true;
    }

    /// @brief Run one PAL frame (fires the frame interrupt) and advance the ULA.
    void run_frame() {
        machine_.RunFrame([this](uint64_t target) {
            const uint64_t before = cpu_.GetCycleCount();
            while (cpu_.GetCycleCount() - before < target && !cpu_.IsHalted()) {
                do { cpu_.Step(); } while (!cpu_.InstructionComplete());
            }
            return cpu_.GetCycleCount() - before;
        });
        ula_.end_frame();
    }

    /// @brief Render the current frame as palette indices (kPixels values).
    void render_indices(std::span<uint8_t> out) const {
        video::render_frame(ula_, ula_.flash_on(), out);
    }

    /// @brief Render the current frame as RGBA8888 (kPixels values; GL-ready).
    void render_rgba(std::span<uint32_t> out) const {
        std::array<uint8_t, video::kFramePixels> indices{};
        video::render_frame(ula_, ula_.flash_on(), indices);
        const std::size_t n = std::min(out.size(), indices.size());
        for (std::size_t i = 0; i < n; ++i) {
            const screen::Rgb c = screen::to_rgb(indices[i]);
            out[i] = 0xFF000000u | (static_cast<uint32_t>(c.b) << 16) |
                     (static_cast<uint32_t>(c.g) << 8) | static_cast<uint32_t>(c.r);
        }
    }

    [[nodiscard]] SpectrumCpu& cpu() noexcept { return cpu_; }
    [[nodiscard]] Ula& ula() noexcept { return ula_; }
    [[nodiscard]] uint64_t frame_count() const noexcept { return ula_.frame_counter(); }

private:
    SpectrumCpu cpu_;
    Ula ula_;
    Machine<SpectrumCpu> machine_;
};

} // namespace z80::machine::spectrum

#endif // Z80_MACHINE_SPECTRUM_SPECTRUM_MACHINE_H
