//
// Z80 Digital Twin - CB-prefixed rotate/shift flag verification
// Copyright (c) 2025-2026 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//
// Regression test for a flag bug: RLC/RRC/RL/RR/SLA/SRA/SRL/SLL must RECOMPUTE
// S, Z and P/V from their result — they were wrongly *preserving* the previous
// values. The 48K ROM's cursor routine does `LD A,(MODE) : RLC A : JR Z`, so a
// stale Z (left by a prior `SBC HL,DE`) made the cursor stay "K" in extended
// mode instead of "E".
//

#include "z80_cpu.h"

#include <cstdint>
#include <iostream>
#include <utility>
#include <vector>

namespace {

using z80::CPU;
constexpr uint8_t kZero = 0x40;
constexpr uint8_t kCarry = 0x01;

int failures = 0;
void check(bool ok, const char* what) {
    std::cout << (ok ? "  ✓ " : "  ✗ ") << what << '\n';
    if (!ok) ++failures;
}

// Run a tiny program at 0x0000 to HALT; return (A, F).
std::pair<uint8_t, uint8_t> run(std::vector<uint8_t> prog) {
    CPU cpu;
    cpu.LoadProgram(prog, 0x0000);
    for (int i = 0; i < 200 && !cpu.IsHalted(); ++i)
        do { cpu.Step(); } while (!cpu.InstructionComplete());
    return {cpu.A(), cpu.F()};
}

} // namespace

int main() {
    std::cout << "CB rotate/shift flag verification\n=================================\n";

    // Each case: LD A,in ; CP A (forces Z=1, carry=0, A unchanged) ; CB op ; HALT.
    // The op's nonzero result MUST clear Z (the bug left the stale Z=1).
    std::cout << "\n[1] Nonzero result clears Z (was stuck set)\n";
    struct Case { const char* name; uint8_t op; uint8_t in; uint8_t out; };
    const Case cases[] = {
        {"RLC A", 0x07, 0x01, 0x02},
        {"RRC A", 0x0F, 0x02, 0x01},
        {"RL A",  0x17, 0x01, 0x02},   // carry=0 from CP A
        {"RR A",  0x1F, 0x02, 0x01},
        {"SLA A", 0x27, 0x01, 0x02},
        {"SRA A", 0x2F, 0x02, 0x01},
        {"SLL A", 0x37, 0x01, 0x03},   // undocumented: shifts a 1 in
        {"SRL A", 0x3F, 0x02, 0x01},
    };
    for (const Case& c : cases) {
        auto [a, f] = run({0x3E, c.in, 0xBF, 0xCB, c.op, 0x76});
        check(a == c.out && (f & kZero) == 0,
              c.name);
    }

    std::cout << "\n[2] Zero result sets Z, and carry = shifted-out bit\n";
    {
        // LD A,1 ; OR A (Z=0) ; SRL A -> 0 (carry=1) ; HALT
        auto [a, f] = run({0x3E, 0x01, 0xB7, 0xCB, 0x3F, 0x76});
        check(a == 0x00, "SRL A: 1 -> 0");
        check((f & kZero) != 0, "Z set on zero result");
        check((f & kCarry) != 0, "carry = shifted-out bit");
    }

    std::cout << "\n[3] The ROM cursor pattern: LD A,(MODE) ; RLC A ; JR Z\n";
    {
        // Seed a 'MODE' byte = 1 at 0x9000, set Z via CP A, then RLC and branch.
        //   LD A,1 ; LD (0x9000),A ; CP A ; LD A,(0x9000) ; RLC A ; JR Z,+2 ; (Z path) LD A,0xAA ; HALT ; (NZ) LD A,2 ; HALT
        auto [a, f] = run({
            0x3E, 0x01,             // LD A,1
            0x32, 0x00, 0x90,       // LD (0x9000),A
            0xBF,                   // CP A    -> Z=1
            0x3A, 0x00, 0x90,       // LD A,(0x9000)  -> A=1 (MODE), flags unchanged (Z still 1)
            0xCB, 0x07,             // RLC A   -> A=2, Z MUST clear
            0x28, 0x02,             // JR Z,+2 -> if Z (bug) skip to the wrong path
            0x3E, 0x02, 0x76,       // (correct) LD A,2 ; HALT
            0x3E, 0xAA, 0x76,       // (bug)     LD A,0xAA ; HALT
        });
        (void)f;
        check(a == 0x02, "fell through (Z cleared) — extended-mode branch, not the K path");
    }

    std::cout << "\n=================================\n";
    if (failures == 0) {
        std::cout << "✅ ALL ROTATE/SHIFT FLAG CHECKS PASSED\n";
        return 0;
    }
    std::cout << "❌ " << failures << " check(s) FAILED\n";
    return 1;
}
