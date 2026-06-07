//
// Z80 Digital Twin - ZX Spectrum 48K machine
// Copyright (c) 2025-2026 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//
// Ties the CPU, the ULA, and the generic frame clock into a running 48K:
//   * CPU = CPUImpl<ObservableMemory, ObservableIo<CallbackIo>> — the same config
//     the debugger drives, so a DebugSession can run this machine directly. The
//     ULA hooks the inner CallbackIo's ports; ObservableIo logs transactions for
//     the I/O panel; ObservableMemory's write hook feeds the beam-accurate
//     screen (16 KB ROM at 0x0000, 48 KB RAM above);
//   * the ULA is given the CPU's clock and a RAM reader, observes display-file
//     writes, and acts as the renderer's FrameSource;
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
#include "io/observable_io.h"
#include "memory/observable_memory.h"
#include "machine.h"
#include "screen.h"
#include "tape.h"
#include "timing.h"
#include "ula.h"
#include "video.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace z80::machine::spectrum {

using SpectrumCpu = z80::CPUImpl<z80::ObservableMemory, z80::ObservableIo<z80::CallbackIo>>;

class SpectrumMachine {
public:
    static constexpr int kWidth  = video::kFrameWidth;
    static constexpr int kHeight = video::kFrameHeight;
    static constexpr int kPixels = video::kFramePixels;

    SpectrumMachine() : machine_(cpu_, timing::kTPerFrame) {
        ula_.set_clock([this] { return cpu_.GetCycleCount(); });
        ula_.set_reader([this](uint16_t addr) { return cpu_.ReadMemory(addr); });
        cpu_.GetIo().inner().OnOut([this](uint16_t port, uint8_t value) { ula_.write_port(port, value); });
        cpu_.GetIo().inner().OnIn([this](uint16_t port) { return ula_.read_port(port); });
        cpu_.GetMemory().AddWriteObserver(
            [this](uint16_t addr, uint8_t old_value, uint8_t new_value) {
                ula_.on_write(addr, old_value, new_value);
            });
        cpu_.GetIo().SetRecording(false);   // the viewer doesn't read the I/O log
        ula_.set_ear_source([this] { return tape_.ear_level(cpu_.GetCycleCount()); });
        cpu_.Reset();
    }

    /// @brief Write-protect the 16 KB ROM region (0x0000–0x3FFF), matching real
    ///        hardware. Off by default so stray ROM writes stay visible for
    ///        diagnosis (the debugger's SMC panel flags them).
    void set_rom_write_protect(bool on) {
        if (on) cpu_.GetMemory().SetWriteProtect(0x0000, 0x3FFF);
        else cpu_.GetMemory().ClearWriteProtect();
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
        ula_.begin_frame();             // drop the previous frame's display-write history
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

    // -- Tape ----------------------------------------------------------------
    bool load_tape(std::span<const uint8_t> image) { return tape_.load(image); }
    void play_tape() { tape_.play(cpu_.GetCycleCount()); }
    void stop_tape() { tape_.stop(); }
    [[nodiscard]] Tape& tape() noexcept { return tape_; }

    [[nodiscard]] SpectrumCpu& cpu() noexcept { return cpu_; }
    [[nodiscard]] Ula& ula() noexcept { return ula_; }
    [[nodiscard]] uint64_t frame_count() const noexcept { return ula_.frame_counter(); }

private:
    SpectrumCpu cpu_;
    Ula ula_;
    Tape tape_;
    Machine<SpectrumCpu> machine_;
};

} // namespace z80::machine::spectrum

#endif // Z80_MACHINE_SPECTRUM_SPECTRUM_MACHINE_H
