//
// Z80 Digital Twin - Machine (frame clock + interrupt source) verification
// Copyright (c) 2025-2026 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//
// Verifies the PAL frame clock end to end:
//   1. RunFrame advances ~one frame of T-states, ticks devices, and carries the
//      per-frame overrun so the long-run average is exact.
//   2. The asserted frame interrupt is actually serviced once per frame — a real
//      IM 1 handler runs through CPU::Interrupt every frame.
//

#include "machine.h"
#include "spectrum/timing.h"
#include "z80_cpu.h"

#include <cstdint>
#include <iostream>
#include <vector>

namespace {

using z80::CPU;
using namespace z80::machine;
namespace t = z80::machine::spectrum::timing;

int failures = 0;
void check(bool ok, const char* what) {
    std::cout << (ok ? "  ✓ " : "  ✗ ") << what << '\n';
    if (!ok) ++failures;
}

struct CountingDevice : Device {
    uint64_t frames = 0;
    void OnFrame() override { ++frames; }
};

// A fast, breakpoint-free stepper: run whole instructions until the T-state
// target is reached (the production-speed path; the debugger would instead pass
// DebugSession::RunForTStates here).
auto make_fast_stepper(CPU& cpu) {
    return [&cpu](uint64_t target) -> uint64_t {
        const uint64_t before = cpu.GetCycleCount();
        while (cpu.GetCycleCount() - before < target && !cpu.IsHalted()) {
            do { cpu.Step(); } while (!cpu.InstructionComplete());
        }
        return cpu.GetCycleCount() - before;
    };
}

} // namespace

int main() {
    std::cout << "Machine frame-clock verification\n================================\n";

    // --- 1. Frame budget, device ticks, carry ------------------------------
    std::cout << "\n[1] RunFrame: frame budget + device OnFrame + carry\n";
    {
        CPU cpu;
        cpu.Reset();
        cpu.LoadProgram({0x18, 0xFE}, 0x0000);   // JR $  (spin forever)

        Machine<CPU> m(cpu, t::kTPerFrame);
        CountingDevice dev;
        m.AddDevice(&dev);
        auto step = make_fast_stepper(cpu);

        uint64_t min_ran = ~0ull;
        for (int i = 0; i < 10; ++i) {
            const uint64_t ran = m.RunFrame(step);
            if (ran < min_ran) min_ran = ran;
        }
        check(m.Frames() == 10, "ran 10 frames");
        check(dev.frames == 10, "device OnFrame called once per frame");
        check(min_ran >= t::kTPerFrame - 30,
              "each frame runs a full frame's worth of T-states (less the carry)");
        check(m.Carry() < 30, "carried overrun is at most one instruction");
    }

    // --- 2. Interrupt serviced once per frame ------------------------------
    std::cout << "\n[2] Frame interrupt is serviced (IM 1 handler runs each frame)\n";
    {
        CPU cpu;
        cpu.Reset();
        // main: IM 1 ; EI ; JR $  (interrupts on, then spin)
        cpu.LoadProgram({0xED, 0x56, 0xFB, 0x18, 0xFE}, 0x0000);
        // handler @ 0x0038: LD HL,0x9000 ; INC (HL) ; EI ; RET
        cpu.LoadProgram({0x21, 0x00, 0x90, 0x34, 0xFB, 0xC9}, 0x0038);

        Machine<CPU> m(cpu, t::kTPerFrame);
        auto step = make_fast_stepper(cpu);

        // Frame 1 primes interrupts: the INT asserted at its start is declined
        // (IFF1 still 0 until EI runs inside the frame).
        m.RunFrame(step);
        const uint8_t primed = cpu.ReadMemory(0x9000);
        check(primed == 0, "no interrupt serviced before EI takes effect");

        for (int i = 0; i < 4; ++i) m.RunFrame(step);
        const uint8_t serviced = cpu.ReadMemory(0x9000);
        check(serviced == 4, "handler ran exactly once per frame after enable");
        check(serviced == m.Frames() - 1, "one serviced interrupt per frame");
    }

    std::cout << "\n================================\n";
    if (failures == 0) {
        std::cout << "✅ ALL MACHINE CHECKS PASSED\n";
        return 0;
    }
    std::cout << "❌ " << failures << " check(s) FAILED\n";
    return 1;
}
