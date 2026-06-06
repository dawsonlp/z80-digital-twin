//
// Z80 Digital Twin - Machine (frame clock + interrupt source)
// Copyright (c) 2025 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//
// Machine drives a CPU in fixed T-state frame quanta — the foundation of a
// timing-accurate machine emulation (the ZX Spectrum's 50 Hz frame being the
// first user). Each frame it asserts the frame interrupt, advances one frame's
// worth of T-states, and ticks its devices.
//
// Decoupling: Machine is a template on the CPU configuration and takes the
// frame runner as a callback, so it depends only on z80_cpu — not on the
// debugger. The caller chooses how the T-states are advanced:
//   * production / fast: a lambda over CPU::RunUntilCycle (raw speed);
//   * debugging: a lambda over DebugSession::RunForTStates (breakpoint-aware).
// This keeps machine and debugger as independent siblings; the application
// composes them.
//

#ifndef Z80_MACHINE_MACHINE_H
#define Z80_MACHINE_MACHINE_H

#include "device.h"

#include <cstdint>
#include <vector>

namespace z80::machine {

template <class Cpu>
class Machine {
public:
    /// @param cpu            The CPU to drive (must outlive the Machine).
    /// @param frame_tstates  T-states per frame (e.g. 69,888 for the PAL 48K).
    /// @param int_bus        Byte placed on the data bus at interrupt acknowledge
    ///                       (0xFF on the Spectrum — RST 38 in IM 0, the IM 2
    ///                       vector low half otherwise).
    Machine(Cpu& cpu, uint64_t frame_tstates, uint8_t int_bus = 0xFF)
        : cpu_(cpu), frame_tstates_(frame_tstates), int_bus_(int_bus) {}

    /// @brief Register a device to receive OnFrame() (non-owning; must outlive us).
    void AddDevice(Device* device) { devices_.push_back(device); }

    /// @brief Run one frame: assert the frame interrupt, advance a frame's worth
    ///        of T-states via @p step, then tick devices.
    /// @param step  Callable `uint64_t(uint64_t target_tstates)` that advances the
    ///              CPU and returns the T-states actually executed (it may overrun
    ///              the target by up to one instruction, or stop short on a
    ///              breakpoint — the remainder is carried into the next frame so
    ///              the long-run average is exactly @p frame_tstates).
    /// @returns T-states actually executed this frame.
    template <class Stepper>
    uint64_t RunFrame(Stepper&& step) {
        // The ULA asserts /INT at the frame boundary; the CPU services it at the
        // next instruction (declined if interrupts are masked or in an EI shadow).
        cpu_.Interrupt(int_bus_);

        const uint64_t target = frame_tstates_ - carry_;
        const uint64_t ran = step(target);
        carry_ = ran > target ? ran - target : 0;

        for (Device* device : devices_) device->OnFrame();
        ++frames_;
        return ran;
    }

    [[nodiscard]] uint64_t Frames() const noexcept { return frames_; }
    [[nodiscard]] uint64_t Carry() const noexcept { return carry_; }
    [[nodiscard]] uint64_t FrameTStates() const noexcept { return frame_tstates_; }

private:
    Cpu& cpu_;
    uint64_t frame_tstates_;
    uint8_t int_bus_;
    uint64_t carry_ = 0;    ///< T-states the last frame overran, owed back next.
    uint64_t frames_ = 0;
    std::vector<Device*> devices_;
};

} // namespace z80::machine

#endif // Z80_MACHINE_MACHINE_H
