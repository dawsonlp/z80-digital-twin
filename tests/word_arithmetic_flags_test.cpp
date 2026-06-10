//
// Z80 Digital Twin - 16-bit arithmetic flag verification
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

void step_program(CPU& cpu, const std::vector<uint8_t>& prog) {
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

void check_flag(uint8_t actual, uint8_t expected, const std::string& what) {
    check_eq(actual, expected, what);
}

} // namespace

int main() {
    std::cout << "16-bit arithmetic flag verification\n===================================\n";

    {
        CPU cpu;
        cpu.HL() = 0x1F00;
        cpu.BC() = 0x0100;
        cpu.F() = kS | kZ | kPV | kN | kC;
        step_program(cpu, {0x09}); // ADD HL,BC
        check_eq(cpu.HL(), 0x2000, "ADD HL,BC result");
        check_flag(cpu.F(), kS | kZ | kPV | kY | kH, "ADD HL preserves S/Z/PV, resets N/C, sets H and high-byte Y");
    }

    {
        CPU cpu;
        cpu.IX() = 0x0700;
        cpu.SP() = 0x0100;
        cpu.F() = kPV;
        step_program(cpu, {0xDD, 0x39}); // ADD IX,SP
        check_eq(cpu.IX(), 0x0800, "ADD IX,SP result");
        check_flag(cpu.F(), kPV | kX, "ADD IX uses result high byte for X/Y and preserves P/V");
    }

    {
        CPU cpu;
        cpu.IY() = 0x0F00;
        cpu.DE() = 0x0100;
        cpu.F() = kC | kZ;
        step_program(cpu, {0xFD, 0x19}); // ADD IY,DE
        check_eq(cpu.IY(), 0x1000, "ADD IY,DE result");
        check_flag(cpu.F(), kZ | kH, "ADD IY preserves Z and sets half-carry from bit 11");
    }

    {
        CPU cpu;
        cpu.HL() = 0x7FFF;
        cpu.BC() = 0x0000;
        cpu.F() = kC;
        step_program(cpu, {0xED, 0x4A}); // ADC HL,BC
        check_eq(cpu.HL(), 0x8000, "ADC HL,BC result");
        check_flag(cpu.F(), kS | kPV | kH, "ADC HL recomputes S/PV/H and high-byte X/Y from result");
    }

    {
        CPU cpu;
        cpu.HL() = 0x0000;
        cpu.SP() = 0x0000;
        cpu.F() = kC;
        step_program(cpu, {0xED, 0x72}); // SBC HL,SP
        check_eq(cpu.HL(), 0xFFFF, "SBC HL,SP result");
        check_flag(cpu.F(), kS | kY | kH | kX | kN | kC, "SBC HL recomputes full flags from 16-bit result");
    }

    std::cout << "\n===================================\n";
    if (failures == 0) {
        std::cout << "ALL 16-BIT ARITHMETIC FLAG CHECKS PASSED\n";
        return 0;
    }
    std::cout << failures << " check(s) FAILED\n";
    return 1;
}
