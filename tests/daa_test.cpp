//
// Z80 Digital Twin - DAA (decimal adjust) verification
// Copyright (c) 2025-2026 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//
// DAA was incomplete: it never set P/V (parity) and preserved H instead of
// recomputing it, with the same correction conditions for add and subtract.
// These cases use verified BCD results.
//

#include "z80_cpu.h"

#include <cstdint>
#include <iostream>
#include <utility>
#include <vector>

namespace {

using z80::CPU;
constexpr uint8_t kC = 0x01, kN = 0x02, kPV = 0x04, kZ = 0x40;

int failures = 0;
void check(bool ok, const char* what) {
    std::cout << (ok ? "  ✓ " : "  ✗ ") << what << '\n';
    if (!ok) ++failures;
}

std::pair<uint8_t, uint8_t> run(std::vector<uint8_t> prog) {
    CPU cpu;
    cpu.LoadProgram(prog, 0x0000);
    for (int i = 0; i < 200 && !cpu.IsHalted(); ++i)
        do { cpu.Step(); } while (!cpu.InstructionComplete());
    return {cpu.A(), cpu.F()};
}

} // namespace

int main() {
    std::cout << "DAA verification\n================\n";

    // 19 + 28 = 47 (BCD).  ADD sets H; DAA adds 0x06 -> 0x47.
    {
        auto [a, f] = run({0x3E, 0x19, 0xC6, 0x28, 0x27, 0x76});
        check(a == 0x47, "19 + 28 = 47 (BCD)");
        check((f & kC) == 0 && (f & kN) == 0, "no carry, N clear (after add)");
        check((f & kPV) != 0, "parity set (0x47 has even parity) — was never computed");
    }
    // 99 + 01 = 00 with carry (BCD).
    {
        auto [a, f] = run({0x3E, 0x99, 0xC6, 0x01, 0x27, 0x76});
        check(a == 0x00, "99 + 01 = 00 (BCD)");
        check((f & kC) != 0 && (f & kZ) != 0, "carry out + zero");
    }
    // 42 - 17 = 25 (BCD).  SUB sets N (and H); DAA subtracts 0x06.
    {
        auto [a, f] = run({0x3E, 0x42, 0xD6, 0x17, 0x27, 0x76});
        check(a == 0x25, "42 - 17 = 25 (BCD)");
        check((f & kN) != 0 && (f & kC) == 0, "N set (after sub), no borrow");
    }
    // 12 - 34 = 78 with borrow (BCD).
    {
        auto [a, f] = run({0x3E, 0x12, 0xD6, 0x34, 0x27, 0x76});
        check(a == 0x78, "12 - 34 = 78 (BCD, with borrow)");
        check((f & kC) != 0 && (f & kN) != 0, "borrow (carry) + N set");
    }

    std::cout << "\n================\n";
    if (failures == 0) {
        std::cout << "✅ ALL DAA CHECKS PASSED\n";
        return 0;
    }
    std::cout << "❌ " << failures << " check(s) FAILED\n";
    return 1;
}
