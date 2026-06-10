//
// Z80 Digital Twin - BIT flag verification
// Copyright (c) 2025-2026 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//

#include "z80_cpu.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

using z80::CPU;

constexpr uint8_t kC = 0x01;
constexpr uint8_t kPV = 0x04;
constexpr uint8_t kX = 0x08;
constexpr uint8_t kH = 0x10;
constexpr uint8_t kY = 0x20;
constexpr uint8_t kZ = 0x40;
constexpr uint8_t kS = 0x80;

int failures = 0;

CPU run(const std::vector<uint8_t>& prog) {
    CPU cpu;
    cpu.LoadProgram(prog, 0x0000);
    for (int i = 0; i < 20 && !cpu.IsHalted(); ++i) {
        do {
            cpu.Step();
        } while (!cpu.InstructionComplete());
    }
    return cpu;
}

void check_eq(uint8_t actual, uint8_t expected, const std::string& what) {
    std::cout << ((actual == expected) ? "  ok " : "  !! ") << what
              << " expected=0x" << std::hex << static_cast<int>(expected)
              << " actual=0x" << static_cast<int>(actual) << std::dec << '\n';
    if (actual != expected) ++failures;
}

} // namespace

int main() {
    std::cout << "BIT flag verification\n=====================\n";

    {
        CPU cpu = run({0x06, 0x28, 0x37, 0xCB, 0x40, 0x76}); // LD B,28h; SCF; BIT 0,B
        check_eq(cpu.F(), kY | kH | kX | kPV | kZ | kC, "BIT copies X/Y from register operand and preserves C");
    }

    {
        CPU cpu = run({0x06, 0x80, 0xCB, 0x78, 0x76}); // LD B,80h; BIT 7,B
        check_eq(cpu.F(), kS | kH, "BIT 7 set copies sign and clears Z/PV");
    }

    {
        CPU cpu = run({0x21, 0x00, 0x20, 0x36, 0x28, 0xCB, 0x46, 0x76}); // BIT 0,(HL)
        check_eq(cpu.F(), kY | kH | kPV | kZ, "BIT (HL) copies X/Y from address high byte");
    }

    {
        CPU cpu = run({0xDD, 0x21, 0x00, 0x28, 0xDD, 0x36, 0x01, 0x00,
                       0xDD, 0xCB, 0x01, 0x46, 0x76}); // BIT 0,(IX+1)
        check_eq(cpu.F(), kY | kX | kH | kPV | kZ, "BIT (IX+d) copies X/Y from effective address high byte");
    }

    std::cout << "\n=====================\n";
    if (failures == 0) {
        std::cout << "ALL BIT FLAG CHECKS PASSED\n";
        return 0;
    }
    std::cout << failures << " check(s) FAILED\n";
    return 1;
}
