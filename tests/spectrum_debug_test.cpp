//
// Z80 Digital Twin - debugger-driven Spectrum verification (headless)
// Copyright (c) 2025-2026 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//
// Proves the unified config: a DebugSession wraps a running DebugSpectrumMachine's CPU
// (DebugCPU == SpectrumCpu) and can breakpoint the ROM. Boots a couple of seconds
// of frames, then breaks at the IM 1 interrupt vector (0x0038) and resumes past
// it. SKIPs cleanly when spec48.rom is absent.
//

#include "debug_session.h"
#include "spectrum/spectrum_machine.h"

#include <cstdint>
#include <array>
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

    // Execution grouping must not change clocks, frames, raster or audio.
    {
        sm::DebugSpectrumMachine batch, stepped;
        const std::vector<uint8_t> code{0x3E, 0x12, 0xD3, 0xFE, 0xEE, 0x10,
                                       0x32, 0x00, 0x58, 0xC3, 0x02, 0x80};
        for (auto* m : {&batch, &stepped}) {
            m->cpu().LoadProgram(code, 0x8000);
            m->cpu().PC() = 0x8000;
        }
        DebugSession batched(batch.cpu()), single(stepped.cpu());
        batched.SetExecutionHooks([&] { batch.prepare_execution(); }, [&] { batch.advance_execution(); });
        single.SetExecutionHooks([&] { stepped.prepare_execution(); }, [&] { stepped.advance_execution(); });
        std::vector<std::pair<uint64_t, uint8_t>> edges_a, edges_b;
        auto capture = [](auto& machine, auto& edges) {
            for (const auto& e : machine.ula().beeper_edges()) edges.emplace_back(e.cycle, e.level);
        };
        batch.on_frame_completed([&] { capture(batch, edges_a); });
        stepped.on_frame_completed([&] { capture(stepped, edges_b); });
        constexpr int count = 25000;
        batched.RunSlice(count);
        for (int i = 0; i < count; ++i) {
            if (i == 37) {
                const auto clock = stepped.cpu().GetCycleCount();
                const auto deadline = stepped.frame_deadline();
                single.AddBreakpoint(stepped.cpu().PC());
                check(single.RunSlice(1).reason == StopReason::Breakpoint, "mid-frame breakpoint stops");
                single.Pause();
                check(stepped.cpu().GetCycleCount() == clock && stepped.frame_deadline() == deadline &&
                      stepped.frame_active() && stepped.frame_count() == 0,
                      "pause preserves clock and unfinished frame");
                single.ClearBreakpoints();
            }
            single.StepInstruction();
        }
        check(batch.cpu().PC() == stepped.cpu().PC() && batch.cpu().AF() == stepped.cpu().AF() &&
              batch.cpu().GetCycleCount() == stepped.cpu().GetCycleCount(),
              "batch and single steps produce identical CPU state and T-states");
        check(batch.frame_count() == stepped.frame_count() &&
              batch.frame_count() == batch.cpu().GetCycleCount() / sm::timing::kTPerFrame &&
              batch.frame_deadline() == stepped.frame_deadline(),
              "frames follow T-state deadlines including instruction overrun");
        std::array<uint8_t, sm::DebugSpectrumMachine::kPixels> a{}, b{};
        batch.render_indices(a); stepped.render_indices(b);
        check(a == b, "batch and stepped completed raster images match");
        check(!edges_a.empty() && edges_a == edges_b, "frame beeper edge timestamps match");
        bool memory_equal = true;
        for (uint32_t address = 0; address < 65536; ++address)
            memory_equal &= batch.cpu().ReadMemory(address) == stepped.cpu().ReadMemory(address);
        check(memory_equal, "batch and stepped memory match");
        // The lightweight viewer policy must preserve the same machine behavior.
        sm::SpectrumMachine lightweight;
        lightweight.cpu().LoadProgram(code, 0x8000);
        lightweight.cpu().PC() = 0x8000;
        std::vector<std::pair<uint64_t, uint8_t>> light_edges;
        lightweight.on_frame_completed([&] { capture(lightweight, light_edges); });
        for (int i = 0; i < count; ++i) {
            lightweight.prepare_execution();
            lightweight.cpu().StepInstruction();
            lightweight.advance_execution();
        }
        std::array<uint8_t, sm::SpectrumMachine::kPixels> light_frame{};
        lightweight.render_indices(light_frame);
        check(lightweight.cpu().GetCycleCount() == batch.cpu().GetCycleCount() &&
              lightweight.cpu().PC() == batch.cpu().PC() &&
              lightweight.frame_count() == batch.frame_count() &&
              lightweight.frame_deadline() == batch.frame_deadline() &&
              light_frame == a && light_edges == edges_a,
              "lightweight and metadata memory preserve T-states, frame deadlines, raster and audio");
        // Starting a new partial frame must not erase the last displayed frame.
        const auto frame = stepped.frame_count();
        single.StepInstruction();
        if (stepped.frame_count() == frame) {
            stepped.render_indices(a);
            check(a == b, "completed screen remains stable while next frame is partial");
        }
    }

    {
        sm::DebugSpectrumMachine machine;
        machine.cpu().LoadProgram({0xCD, 0x03, 0x80}, 0x8000);
        machine.cpu().PC() = 0x8000;
        machine.cpu().SP() = 0x9000;
        machine.cpu().IFF1() = true;
        DebugSession session(machine.cpu());
        session.SetExecutionHooks([&] { machine.prepare_execution(); }, [&] { machine.advance_execution(); });
        const auto step = session.StepOver();
        check(step.reason == StopReason::StepComplete && step.pc == 0x0039 && step.cycles == 17,
              "step-over decodes after the machine interrupt, not the interrupted CALL");
        machine.cpu().WriteMemory(0x0039, 0x76);
        session.StepInstruction();
        check(machine.cpu().IsHalted() && session.CanAdvance(),
              "debugger can request machine execution while CPU is halted");
        machine.cpu().IFF1() = true;
        const auto wake = session.StepInstruction();
        check(wake.reason == StopReason::StepComplete && wake.pc == 0x0039,
              "ordinary machine interrupt can wake a halted single step");
    }

    const std::vector<uint8_t> rom = find_rom();
    if (rom.empty()) {
        std::cout << "  SKIP: spec48.rom not found\n";
        return failures ? 1 : 0;
    }

    sm::DebugSpectrumMachine machine;
    machine.load_rom(rom);

    // A DebugSession drives the very same CPU the machine runs (one config).
    DebugSession session(machine.cpu());
    session.SetExecutionHooks([&] { machine.prepare_execution(); },
                              [&] { machine.advance_execution(); });

    // Boot to BASIC (interrupts enabled, ROM idling on HALT) via the machine.
    for (int i = 0; i < 120; ++i) machine.run_frame();
    check(machine.frame_count() == 120, "booted 120 frames");

    // Break at the IM 1 interrupt handler. Firing the frame interrupt vectors the
    // CPU to 0x0038 (waking any HALT); the session must stop there.
    session.AddBreakpoint(0x0038);
    session.Run();
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
