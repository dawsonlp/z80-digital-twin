//
// Z80 Digital Twin - R (memory-refresh) register verification
// Copyright (c) 2025-2026 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//
// The R register's low 7 bits increment on every M1 opcode fetch; bit 7 is
// preserved (changed only by LD R,A). Prefixed instructions have two M1 fetches
// and so bump R twice. Refresh-keyed self-decrypting loaders (e.g. Speedlock,
// used by Arkanoid) read R via LD A,R as a decryption key — without the per-M1
// increment the key is constant and the decrypt produces garbage.
//

#include "z80_cpu.h"

#include <cstdint>
#include <iostream>
#include <vector>

using z80::CPU;

namespace {
int failures = 0;
void check(bool ok, const char* what) {
    std::cout << (ok ? "  ✓ " : "  ✗ ") << what << '\n';
    if (!ok) ++failures;
}

// Load a program at 0x8000, point PC at it, and run it instruction by instruction.
CPU make(const std::vector<uint8_t>& prog) {
    CPU cpu;
    cpu.Reset();
    cpu.LoadProgram(prog, 0x8000);
    cpu.PC() = 0x8000;
    return cpu;
}
void step(CPU& cpu) { do { cpu.Step(); } while (!cpu.InstructionComplete()); }
} // namespace

int main() {
    std::cout << "R (refresh) register verification\n=================================\n";

    check(CPU{}.R() == 0, "R is 0 after construction/reset");

    // Seed R = 0x80 via LD R,A (A=0x80), then two NOPs each bump R by 1.
    {
        CPU cpu = make({0x3E, 0x80, 0xED, 0x4F, 0x00, 0x00, 0x76});  // LD A,80; LD R,A; NOP; NOP; HALT
        step(cpu);                                   // LD A,0x80
        step(cpu);                                   // LD R,A   -> R = 0x80
        check(cpu.R() == 0x80, "LD R,A sets R");
        step(cpu);                                   // NOP
        check(cpu.R() == 0x81, "NOP bumps R by 1 (M1)");
        step(cpu);                                   // NOP
        check(cpu.R() == 0x82, "second NOP bumps R again");
    }

    // Bit 7 is preserved across the low-7-bit wrap (0x7F -> 0x00, keep bit 7).
    {
        CPU cpu = make({0x3E, 0xFF, 0xED, 0x4F, 0x00, 0x76});  // LD A,FF; LD R,A; NOP; HALT
        step(cpu); step(cpu);                        // R = 0xFF (bit7=1, low7=0x7F)
        step(cpu);                                   // NOP: low7 0x7F+1 -> 0x00, bit7 kept
        check(cpu.R() == 0x80, "low 7 bits wrap, bit 7 preserved");
    }

    // A prefixed instruction has two M1 fetches -> R increments by 2.
    {
        CPU cpu = make({0x3E, 0x80, 0xED, 0x4F, 0xED, 0x44, 0x76});  // LD A,80; LD R,A; NEG; HALT
        step(cpu); step(cpu);                        // R = 0x80
        step(cpu);                                   // NEG (ED 44): two M1 -> +2
        check(cpu.R() == 0x82, "ED-prefixed instruction bumps R by 2");
    }

    // LD A,R reads the live, incrementing R (its own fetch is counted).
    {
        CPU cpu = make({0x3E, 0x80, 0xED, 0x4F, 0xED, 0x5F, 0x76});  // LD A,80; LD R,A; LD A,R; HALT
        step(cpu); step(cpu);                        // R = 0x80
        step(cpu);                                   // LD A,R (ED 5F): R 0x80 -> 0x82, A := R
        check(cpu.A() == 0x82, "LD A,R returns the incremented R");
        check(cpu.R() == 0x82, "R holds 0x82 after LD A,R");
    }

    std::cout << (failures == 0 ? "\nAll R-register checks passed.\n" : "\nFAILURES above.\n");
    return failures == 0 ? 0 : 1;
}
