//
// Z80 Digital Twin - ZX Spectrum 48K machine
// Copyright (c) 2025-2026 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//
// Ties the CPU, the ULA, and the generic frame clock into a running 48K:
//   * CPU memory is selectable: ObservableMemory for the viewer, MetadataMemory
//     for the debugger. Both use identical instruction and frame scheduling. The
//     ULA hooks the inner CallbackIo's ports; ObservableIo logs transactions for
//     the I/O panel; ObservableMemory's write hook feeds the beam-accurate
//     screen (16 KB ROM at 0x0000, 48 KB RAM above);
//   * the ULA is given the CPU's clock and a RAM reader, observes display-file
//     writes, and acts as the renderer's FrameSource;
//   * instruction execution advances persistent frame deadlines; frame batching
//     is a convenience over the same instruction lifecycle.
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
#include "memory/metadata_memory.h"
#include "screen.h"
#include "tape.h"
#include "timing.h"
#include "ula.h"
#include "video.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace z80::machine::spectrum {

// Select a memory policy at construction/type selection; frame/ULA logic is shared.
// Machine memory must also provide write observers, protection and RawWrite.
template<class Memory>
class SpectrumMachineImpl {
public:
    using Cpu = z80::CPUImpl<Memory, z80::ObservableIo<z80::CallbackIo>>;
    static constexpr int kWidth  = video::kFrameWidth;
    static constexpr int kHeight = video::kFrameHeight;
    static constexpr int kPixels = video::kFramePixels;

    SpectrumMachineImpl() {
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

    SpectrumMachineImpl(const SpectrumMachineImpl&) = delete;
    SpectrumMachineImpl& operator=(const SpectrumMachineImpl&) = delete;
    SpectrumMachineImpl(SpectrumMachineImpl&&) = delete;
    SpectrumMachineImpl& operator=(SpectrumMachineImpl&&) = delete;

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
        reset_timing();
        cpu_.Reset();
        return true;
    }

    // These two hooks bracket CPU execution regardless of the caller's run
    // quantum. Debuggers prepare before checking breakpoints, and advance after
    // every completed instruction or explicit partial-prefix result.
    void prepare_execution() {
        if (frame_active_) return;
        ula_.begin_frame();
        frame_active_ = true;
        cpu_.Interrupt(0xFF); // existing machine-originated one-shot frame model
    }

    void advance_execution() {
        const uint64_t now = cpu_.GetCycleCount();
        if (!frame_active_) return;
        // Preserve the existing early-HALT frame policy for now. This is not a
        // model of halted bus cycles; the separate fidelity increment owns it.
        if (now < frame_deadline_ && !cpu_.IsHalted()) return;
        const uint64_t boundary = cpu_.IsHalted() && now < frame_deadline_
                                      ? now : frame_deadline_;
        ula_.end_frame(boundary);
        video::render_frame(ula_, ula_.flash_on(), completed_frame_);
        has_completed_frame_ = true;
        frame_active_ = false;
        frame_deadline_ = boundary + timing::kTPerFrame;
        if (frame_completed_) frame_completed_();
    }

    // At most 4096 stages per action keeps pathological prefix chains bounded.
    InstructionResult step_instruction(uint32_t stage_budget = 4096) {
        if (stage_budget == 0) return {InstructionStop::BudgetExhausted, 0, 0, false};
        const uint64_t before = cpu_.GetCycleCount();
        prepare_execution();
        auto result = cpu_.StepInstruction(std::min(stage_budget, uint32_t{4096}));
        advance_execution();
        result.cycles = cpu_.GetCycleCount() - before;
        return result;
    }

    /// Convenience batching only: exactly the same lifecycle as single steps.
    void run_frame() {
        const auto frame = frame_count();
        while (frame_count() == frame) {
            const auto result = step_instruction();
            if (result.stop == InstructionStop::BudgetExhausted) break;
        }
    }

    void on_frame_completed(std::function<void()> callback) {
        frame_completed_ = std::move(callback);
    }
    [[nodiscard]] bool frame_active() const { return frame_active_; }
    [[nodiscard]] uint64_t frame_deadline() const { return frame_deadline_; }
    void reset_timing() {
        ula_.reset();
        frame_active_ = false;
        frame_deadline_ = timing::kTPerFrame;
        has_completed_frame_ = false;
    }

    /// @brief Render the current frame as palette indices (kPixels values).
    void render_indices(std::span<uint8_t> out) const {
        if (has_completed_frame_) {
            std::copy_n(completed_frame_.begin(), std::min(out.size(), completed_frame_.size()), out.begin());
        } else {
            video::render_frame(ula_, ula_.flash_on(), out);
        }
    }

    /// @brief Render the current frame as RGBA8888 (kPixels values; GL-ready).
    void render_rgba(std::span<uint32_t> out) const {
        std::array<uint8_t, video::kFramePixels> indices{};
        render_indices(indices);
        const std::size_t n = std::min(out.size(), indices.size());
        for (std::size_t i = 0; i < n; ++i) {
            const screen::Rgb c = screen::to_rgb(indices[i]);
            out[i] = 0xFF000000u | (static_cast<uint32_t>(c.b) << 16) |
                     (static_cast<uint32_t>(c.g) << 8) | static_cast<uint32_t>(c.r);
        }
    }

    // Device state only; CPU and memory are captured by the session controller.
    struct DeviceSnapshot {
        Ula::Snapshot ula;
        Tape::Snapshot tape;
        bool frame_active;
        uint64_t frame_deadline;
        std::array<uint8_t, video::kFramePixels> completed_frame;
        bool has_completed_frame;
        bool operator==(const DeviceSnapshot&) const = default;
    };
    [[nodiscard]] DeviceSnapshot CaptureDevices() const {
        return {ula_.CaptureState(), tape_.CaptureState(), frame_active_, frame_deadline_,
                completed_frame_, has_completed_frame_};
    }
    // All allocation/copying occurs before entry. Live wiring is retained.
    void RestoreDevices(DeviceSnapshot state) noexcept {
        ula_.RestoreState(std::move(state.ula));
        tape_.RestoreState(std::move(state.tape));
        frame_active_ = state.frame_active; frame_deadline_ = state.frame_deadline;
        completed_frame_ = state.completed_frame;
        has_completed_frame_ = state.has_completed_frame;
    }

    // -- Tape ----------------------------------------------------------------
    bool load_tape(std::span<const uint8_t> image) { return tape_.load(image); }
    void play_tape() { tape_.play(cpu_.GetCycleCount()); }
    void stop_tape() { tape_.stop(); }
    [[nodiscard]] Tape& tape() noexcept { return tape_; }

    [[nodiscard]] Cpu& cpu() noexcept { return cpu_; }
    [[nodiscard]] Ula& ula() noexcept { return ula_; }
    [[nodiscard]] uint64_t frame_count() const noexcept { return ula_.frame_counter(); }

private:
    Cpu cpu_;
    Ula ula_;
    Tape tape_;
    bool frame_active_ = false;
    uint64_t frame_deadline_ = timing::kTPerFrame;
    std::function<void()> frame_completed_;
    std::array<uint8_t, video::kFramePixels> completed_frame_{};
    bool has_completed_frame_ = false;
};

using SpectrumMachine = SpectrumMachineImpl<z80::ObservableMemory>;
using DebugSpectrumMachine = SpectrumMachineImpl<z80::MetadataMemory>;
using SpectrumCpu = SpectrumMachine::Cpu;
using DebugSpectrumCpu = DebugSpectrumMachine::Cpu;

} // namespace z80::machine::spectrum

#endif // Z80_MACHINE_SPECTRUM_SPECTRUM_MACHINE_H
