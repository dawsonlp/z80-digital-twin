//
// Z80 Digital Twin - debugger-driven Spectrum verification (headless)
// Copyright (c) 2025-2026 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//
// Proves the unified config: a DebugSession wraps a running SpectrumMachine's CPU
// (DebugCPU == SpectrumCpu) and can breakpoint the ROM. Boots a couple of seconds
// of frames, then breaks at the IM 1 interrupt vector (0x0038) and resumes past
// it. SKIPs cleanly when spec48.rom is absent.
//

#include "debug_session.h"
#include "spectrum/spectrum_machine.h"

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

namespace sm = z80::machine::spectrum;
using namespace z80::dbg;

int failures = 0;
void check(bool ok, const char* what) {
    std::cout << (ok ? "  ✓ " : "  ✗ ") << what << '\n';
    if (!ok) ++failures;
}

std::vector<uint8_t> find_rom() {
    std::vector<std::string> paths;
    if (const char* env = std::getenv("Z80_SPEC48_ROM")) paths.emplace_back(env);
    paths.insert(paths.end(), {"spec48.rom", "../spec48.rom", "../../spec48.rom"});
    for (const auto& p : paths) {
        std::ifstream f(p, std::ios::binary);
        if (f) return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)),
                                           std::istreambuf_iterator<char>());
    }
    return {};
}

} // namespace

int main() {
    std::cout << "Debugger-driven Spectrum verification\n=====================================\n";

    const std::vector<uint8_t> rom = find_rom();
    if (rom.empty()) {
        std::cout << "  SKIP: spec48.rom not found\n";
        return 0;
    }

    sm::SpectrumMachine machine;
    machine.load_rom(rom);

    // A DebugSession drives the very same CPU the machine runs (one config).
    DebugSession session(machine.cpu());

    // Boot to BASIC (interrupts enabled, ROM idling on HALT) via the machine.
    for (int i = 0; i < 120; ++i) machine.run_frame();
    check(machine.frame_count() == 120, "booted 120 frames");

    // Break at the IM 1 interrupt handler. Firing the frame interrupt vectors the
    // CPU to 0x0038 (waking any HALT); the session must stop there.
    session.AddBreakpoint(0x0038);
    session.Run();
    machine.cpu().Interrupt(0xFF);
    const StepResult hit = session.RunForTStates(4000);
    check(hit.reason == StopReason::Breakpoint, "stopped at a breakpoint");
    check(machine.cpu().PC() == 0x0038, "paused at the IM 1 vector (0x0038)");
    check(session.State() == RunState::Paused, "session Paused at the breakpoint");

    // Resume past it: the breakpoint is skipped once, the handler runs.
    session.RemoveBreakpoint(0x0038);
    session.Run();
    const StepResult on = session.RunForTStates(4000);
    check(machine.cpu().PC() != 0x0038, "resumed past the breakpoint (handler ran)");
    check(on.cycles > 0, "executed instructions after resume");

    // The session also sees memory the ROM touched (dirty tracking works on the
    // running machine — i.e. inspection is live).
    check(!session.DirtyAddresses().empty(), "memory writes observed during the run");

    std::cout << "\n=====================================\n";
    if (failures == 0) {
        std::cout << "✅ DEBUGGER DRIVES THE SPECTRUM\n";
        return 0;
    }
    std::cout << "❌ " << failures << " check(s) FAILED\n";
    return 1;
}
