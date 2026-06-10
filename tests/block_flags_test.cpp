//
// Z80 Digital Twin - block instruction flag verification
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
constexpr uint8_t kN = 0x02;
constexpr uint8_t kPV = 0x04;
constexpr uint8_t kX = 0x08;
constexpr uint8_t kH = 0x10;
constexpr uint8_t kY = 0x20;
constexpr uint8_t kZ = 0x40;
constexpr uint8_t kS = 0x80;

int failures = 0;

void step(CPU& cpu, const std::vector<uint8_t>& prog) {
    cpu.LoadProgram(prog, 0x0000);
    cpu.PC() = 0;
    do {
        cpu.Step();
    } while (!cpu.InstructionComplete());
}

void check_eq(uint16_t actual, uint16_t expected, const std::string& what) {
    std::cout << ((actual == expected) ? "  ok " : "  !! ") << what
              << " expected=0x" << std::hex << expected
              << " actual=0x" << actual << std::dec << '\n';
    if (actual != expected) ++failures;
}

} // namespace

int main() {
    std::cout << "Block instruction flag verification\n===================================\n";

    {
        CPU cpu;
        cpu.A() = 0x01;
        cpu.HL() = 0x2000;
        cpu.DE() = 0x2100;
        cpu.BC() = 0x0002;
        cpu.F() = kS | kZ | kH | kN | kC;
        cpu.WriteMemory(0x2000, 0x07);
        step(cpu, {0xED, 0xA0}); // LDI; A + value = 08h -> F3
        check_eq(cpu.ReadMemory(0x2100), 0x07, "LDI copies byte");
        check_eq(cpu.F(), kS | kZ | kPV | kX | kC, "LDI preserves S/Z/C, resets H/N, sets PV and F3 from A+value");
    }

    {
        CPU cpu;
        cpu.A() = 0x20;
        cpu.HL() = 0x2000;
        cpu.DE() = 0x2100;
        cpu.BC() = 0x0001;
        cpu.F() = kC;
        cpu.WriteMemory(0x2000, 0x02);
        step(cpu, {0xED, 0xA8}); // LDD; A + value = 22h -> F5
        check_eq(cpu.ReadMemory(0x2100), 0x02, "LDD copies byte");
        check_eq(cpu.F(), kY | kC, "LDD clears PV at final count and sets F5 from bit 1 of A+value");
    }

    {
        CPU cpu;
        cpu.A() = 0x20;
        cpu.HL() = 0x2000;
        cpu.BC() = 0x0002;
        cpu.F() = kC;
        cpu.WriteMemory(0x2000, 0x01);
        step(cpu, {0xED, 0xA1}); // CPI; result=1Fh, half set, xy source=1Eh -> F3/F5
        check_eq(cpu.F(), kY | kH | kX | kPV | kN | kC, "CPI uses result-minus-half-borrow for F3/F5");
    }

    {
        CPU cpu;
        cpu.A() = 0x22;
        cpu.HL() = 0x2000;
        cpu.BC() = 0x0001;
        cpu.WriteMemory(0x2000, 0x22);
        step(cpu, {0xED, 0xA9}); // CPD; match, final count
        check_eq(cpu.F(), kZ | kN, "CPD sets Z/N and clears PV at final count");
    }

    std::cout << "\n===================================\n";
    if (failures == 0) {
        std::cout << "ALL BLOCK FLAG CHECKS PASSED\n";
        return 0;
    }
    std::cout << failures << " check(s) FAILED\n";
    return 1;
}
