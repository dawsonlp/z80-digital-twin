//
// Z80 Digital Twin - result-derived flag source verification
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
constexpr uint8_t kY = 0x20;
constexpr uint8_t kS = 0x80;

int failures = 0;

CPU run(const std::vector<uint8_t>& prog) {
    CPU cpu;
    cpu.LoadProgram(prog, 0x0000);
    for (int i = 0; i < 30 && !cpu.IsHalted(); ++i) {
        do {
            cpu.Step();
        } while (!cpu.InstructionComplete());
    }
    return cpu;
}

void check_eq(uint16_t actual, uint16_t expected, const std::string& what) {
    std::cout << ((actual == expected) ? "  ok " : "  !! ") << what
              << " expected=0x" << std::hex << expected
              << " actual=0x" << actual << std::dec << '\n';
    if (actual != expected) ++failures;
}

} // namespace

int main() {
    std::cout << "Result flag source verification\n===============================\n";

    {
        CPU cpu = run({0x3E, 0xE0, 0xED, 0x44, 0x76}); // NEG -> 20h
        check_eq(cpu.A(), 0x20, "NEG result");
        check_eq(cpu.F(), kY | kN | kC, "NEG takes X/Y from result");
    }

    {
        CPU cpu = run({0x3E, 0x14, 0xB7, 0x07, 0x76}); // OR A; RLCA -> 28h
        check_eq(cpu.A(), 0x28, "RLCA result");
        check_eq(cpu.F(), kY | kX | kPV, "RLCA preserves S/Z/PV and takes X/Y from A");
    }

    {
        CPU cpu = run({0x3E, 0x51, 0x37, 0x17, 0x76}); // SCF; RLA -> A3h
        check_eq(cpu.A(), 0xA3, "RLA result");
        check_eq(cpu.F(), kY, "RLA takes X/Y from A and updates carry");
    }

    {
        CPU cpu = run({0x06, 0x14, 0xCB, 0x00, 0x76}); // RLC B -> 28h
        check_eq(cpu.B(), 0x28, "RLC B result");
        check_eq(cpu.F(), kY | kX | kPV, "CB rotate takes X/Y from result");
    }

    {
        CPU cpu = run({0x06, 0x10, 0xCB, 0x30, 0x76}); // SLL B -> 21h
        check_eq(cpu.B(), 0x21, "SLL B result");
        check_eq(cpu.F(), kY | kPV, "CB SLL takes X/Y from result");
    }

    {
        CPU cpu = run({0x21, 0x00, 0x20, 0x36, 0x12, 0x3E, 0xA0, 0xED, 0x67, 0x76}); // RRD
        check_eq(cpu.A(), 0xA2, "RRD result");
        check_eq(cpu.ReadMemory(0x2000), 0x01, "RRD memory result");
        check_eq(cpu.F(), kS | kY, "RRD takes X/Y from A result");
    }

    {
        CPU cpu = run({0x21, 0x00, 0x20, 0x36, 0x12, 0x3E, 0xA0, 0xED, 0x6F, 0x76}); // RLD
        check_eq(cpu.A(), 0xA1, "RLD result");
        check_eq(cpu.ReadMemory(0x2000), 0x20, "RLD memory result");
        check_eq(cpu.F(), kS | kY, "RLD takes X/Y from A result");
    }

    std::cout << "\n===============================\n";
    if (failures == 0) {
        std::cout << "ALL RESULT FLAG SOURCE CHECKS PASSED\n";
        return 0;
    }
    std::cout << failures << " check(s) FAILED\n";
    return 1;
}
