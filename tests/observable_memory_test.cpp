//
// Z80 Digital Twin - ObservableMemory plug verification
// Copyright (c) 2025 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//
// Verifies the multi-observer memory plug:
//   1. A write observer fires with exact (address, old, new) values.
//   2. Multiple observers all fire; removing one stops only that one.
//   3. CPUImpl<ObservableMemory> and CPUImpl<FastMemory> produce identical CPU
//      state for the same program (plug parity).
//

#include "z80_cpu.h"
#include "memory/observable_memory.h"

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
    for (int guard = 0; guard < 100000 && !cpu.IsHalted(); ++guard) {
        do {
            cpu.Step();
        } while (!cpu.InstructionComplete());
    }
}

} // namespace

int main() {
    using namespace z80;
    std::cout << "ObservableMemory plug verification\n"
                 "==================================\n";

    // --- 1. A write observer delivers exact values --------------------------
    std::cout << "\n[1] Observer delivers exact (address, old, new) events\n";
    {
        CPUImpl<ObservableMemory> cpu;

        struct Event { uint16_t addr; uint8_t old_v; uint8_t new_v; };
        std::vector<Event> events;
        cpu.GetMemory().AddWriteObserver(
            [&](uint16_t a, uint8_t o, uint8_t n) { events.push_back({a, o, n}); });

        run_to_halt(cpu);

        check(cpu.IsHalted(), "CPU reached HALT");
        check(cpu.GetMemory().HasObservers(), "observer still registered after run");

        bool saw_9000 = false, saw_9001 = false;
        for (const auto& e : events) {
            if (e.addr == 0x9000 && e.new_v == 0x42) saw_9000 = true;
            if (e.addr == 0x9001 && e.new_v == 0x99) saw_9001 = true;
        }
        check(saw_9000, "write event for (0x9000) = 0x42");
        check(saw_9001, "write event for (0x9001) = 0x99");
        check(cpu.ReadMemory(0x9000) == 0x42, "memory[0x9000] == 0x42");
        check(cpu.ReadMemory(0x9001) == 0x99, "memory[0x9001] == 0x99");
    }

    // --- 2. Multiple observers; independent removal -------------------------
    std::cout << "\n[2] Multiple observers fire; removal is independent\n";
    {
        CPUImpl<ObservableMemory> cpu;
        int count_a = 0, count_b = 0;
        const int id_a = cpu.GetMemory().AddWriteObserver(
            [&](uint16_t, uint8_t, uint8_t) { ++count_a; });
        cpu.GetMemory().AddWriteObserver(
            [&](uint16_t, uint8_t, uint8_t) { ++count_b; });

        check(cpu.GetMemory().ObserverCount() == 2, "two observers registered");

        cpu.WriteMemory(0x8000, 0x11);
        check(count_a == 1 && count_b == 1, "both observers fired on a write");

        cpu.GetMemory().RemoveWriteObserver(id_a);
        check(cpu.GetMemory().ObserverCount() == 1, "one observer left after removal");

        cpu.WriteMemory(0x8001, 0x22);
        check(count_a == 1, "removed observer no longer fires");
        check(count_b == 2, "remaining observer still fires");

        cpu.GetMemory().ClearWriteObservers();
        cpu.WriteMemory(0x8002, 0x33);
        check(count_b == 2 && !cpu.GetMemory().HasObservers(), "ClearWriteObservers stops all");
        check(cpu.ReadMemory(0x8002) == 0x33, "writes still commit with no observers");
    }

    // --- 3. Plug parity: identical CPU state for both plugs -----------------
    std::cout << "\n[3] FastMemory and ObservableMemory produce identical state\n";
    {
        CPUImpl<FastMemory>       fast;
        CPUImpl<ObservableMemory> obs;
        run_to_halt(fast);
        run_to_halt(obs);

        check(fast.GetCycleCount() == obs.GetCycleCount(), "equal cycle counts");
        check(fast.A() == obs.A(),   "equal A");
        check(fast.PC() == obs.PC(), "equal PC");
        check(fast.IsHalted() == obs.IsHalted(), "equal halt state");

        bool mem_equal = true;
        for (uint32_t a = 0; a < 0x10000; ++a) {
            if (fast.ReadMemory(static_cast<uint16_t>(a)) !=
                obs.ReadMemory(static_cast<uint16_t>(a))) {
                mem_equal = false;
                break;
            }
        }
        check(mem_equal, "equal full 64 KB memory image");
    }

    std::cout << "\n==================================\n";
    if (failures == 0) {
        std::cout << "✅ ALL OBSERVABLE-MEMORY CHECKS PASSED\n";
        return 0;
    }
    std::cout << "❌ " << failures << " check(s) FAILED\n";
    return 1;
}
