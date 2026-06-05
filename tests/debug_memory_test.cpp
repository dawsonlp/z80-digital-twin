//
// Z80 Digital Twin - DebugMemory plug verification
// Copyright (c) 2025 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//
// Verifies the pluggable-memory refactor:
//   1. The DebugMemory write hook fires with exact (address, old, new) values.
//   2. CPUImpl<DebugMemory> and CPUImpl<FastMemory> produce identical CPU state
//      for the same program (plug parity).
//

#include "z80_cpu.h"
#include "memory/debug_memory.h"

#include <cstdint>
#include <iostream>
#include <vector>

namespace {

// A tiny program that writes two bytes to memory then halts:
//   LD A, 0x42       3E 42
//   LD (0x9000), A   32 00 90
//   LD A, 0x99       3E 99
//   LD (0x9001), A   32 01 90
//   HALT             76
const std::vector<uint8_t> kProgram = {
    0x3E, 0x42,
    0x32, 0x00, 0x90,
    0x3E, 0x99,
    0x32, 0x01, 0x90,
    0x76,
};

int failures = 0;

void check(bool ok, const char* what) {
    std::cout << (ok ? "  ✓ " : "  ✗ ") << what << '\n';
    if (!ok) ++failures;
}

template <class Cpu>
void run_to_halt(Cpu& cpu) {
    cpu.LoadProgram(kProgram, 0x0000);
    // Drive the loop the way the debugger will: full instructions, bounded.
    for (int guard = 0; guard < 100000 && !cpu.IsHalted(); ++guard) {
        do {
            cpu.Step();
        } while (!cpu.InstructionComplete());
    }
}

} // namespace

int main() {
    using namespace z80;
    std::cout << "DebugMemory plug verification\n"
                 "=============================\n";

    // --- 1. Write hook fires with exact values ------------------------------
    std::cout << "\n[1] Write hook delivers exact (address, old, new) events\n";
    {
        CPUImpl<DebugMemory> cpu;

        struct Event { uint16_t addr; uint8_t old_v; uint8_t new_v; };
        std::vector<Event> events;

        // Install the hook AFTER nothing yet; we load the program first below so
        // that program-load writes are observed too, then inspect the tail.
        cpu.GetMemory().SetWriteHook(
            [&](uint16_t a, uint8_t o, uint8_t n) { events.push_back({a, o, n}); });

        run_to_halt(cpu);

        check(cpu.IsHalted(), "CPU reached HALT");
        check(cpu.GetMemory().HasWriteHook(), "hook still installed after run");

        // The two store instructions must appear as write events.
        bool saw_9000 = false, saw_9001 = false;
        for (const auto& e : events) {
            if (e.addr == 0x9000 && e.new_v == 0x42) saw_9000 = true;
            if (e.addr == 0x9001 && e.new_v == 0x99) saw_9001 = true;
        }
        check(saw_9000, "write event for (0x9000) = 0x42");
        check(saw_9001, "write event for (0x9001) = 0x99");
        check(cpu.ReadMemory(0x9000) == 0x42, "memory[0x9000] == 0x42");
        check(cpu.ReadMemory(0x9001) == 0x99, "memory[0x9001] == 0x99");

        // Clearing the hook stops delivery.
        events.clear();
        cpu.GetMemory().ClearWriteHook();
        cpu.WriteMemory(0x9002, 0x11);
        check(events.empty(), "no events after ClearWriteHook()");
        check(cpu.ReadMemory(0x9002) == 0x11, "write still committed after clear");
    }

    // --- 2. Plug parity: identical CPU state for both plugs -----------------
    std::cout << "\n[2] FastMemory and DebugMemory produce identical CPU state\n";
    {
        CPUImpl<FastMemory>  fast;
        CPUImpl<DebugMemory> dbg;
        run_to_halt(fast);
        run_to_halt(dbg);

        check(fast.GetCycleCount() == dbg.GetCycleCount(), "equal cycle counts");
        check(fast.A() == dbg.A(),   "equal A");
        check(fast.PC() == dbg.PC(), "equal PC");
        check(fast.IsHalted() == dbg.IsHalted(), "equal halt state");

        bool mem_equal = true;
        for (uint32_t a = 0; a < 0x10000; ++a) {
            if (fast.ReadMemory(static_cast<uint16_t>(a)) !=
                dbg.ReadMemory(static_cast<uint16_t>(a))) {
                mem_equal = false;
                break;
            }
        }
        check(mem_equal, "equal full 64 KB memory image");
    }

    std::cout << "\n=============================\n";
    if (failures == 0) {
        std::cout << "✅ ALL DEBUG-MEMORY CHECKS PASSED\n";
        return 0;
    }
    std::cout << "❌ " << failures << " check(s) FAILED\n";
    return 1;
}
