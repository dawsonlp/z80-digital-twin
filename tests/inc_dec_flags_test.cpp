//
// Z80 Digital Twin - INC/DEC flag verification
// Copyright (c) 2025-2026 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//

#include "z80_cpu.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

using z80::CPU;

constexpr uint8_t kC = 0x01;
constexpr uint8_t kN = 0x02;
constexpr uint8_t kPV = 0x04;
constexpr uint8_t kX = 0x08;
constexpr uint8_t kH = 0x10;
constexpr uint8_t kY = 0x20;
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

void check_eq(uint16_t actual, uint16_t expected, const std::string& what) {
    std::cout << ((actual == expected) ? "  ok " : "  !! ") << what
              << " expected=0x" << std::hex << expected
              << " actual=0x" << actual << std::dec << '\n';
    if (actual != expected) ++failures;
}

} // namespace

int main() {
    std::cout << "INC/DEC flag verification\n=========================\n";

    {
        CPU cpu = run({0x06, 0x1F, 0x37, 0x04, 0x76}); // LD B,1Fh; SCF; INC B
        check_eq(cpu.B(), 0x20, "INC B result");
        check_eq(cpu.F(), kY | kH | kC, "INC sets result Y/H and preserves C");
    }

    {
        CPU cpu = run({0x0E, 0x20, 0x37, 0x0D, 0x76}); // LD C,20h; SCF; DEC C
        check_eq(cpu.C(), 0x1F, "DEC C result");
        check_eq(cpu.F(), kX | kH | kN | kC, "DEC sets result X/H/N and preserves C");
    }

    {
        CPU cpu = run({0x3E, 0x7F, 0x3C, 0x76}); // INC A
        check_eq(cpu.A(), 0x80, "INC A overflow result");
        check_eq(cpu.F(), kS | kPV | kH, "INC sets overflow at 7Fh->80h");
    }

    {
        CPU cpu = run({0x21, 0x00, 0x20, 0x36, 0x1F, 0x34, 0x76}); // INC (HL)
        check_eq(cpu.ReadMemory(0x2000), 0x20, "INC (HL) result");
        check_eq(cpu.F(), kY | kH, "INC (HL) uses memory result for X/Y");
    }

    {
        CPU cpu = run({0xDD, 0x21, 0x00, 0x20, 0xDD, 0x26, 0x1F, 0xDD, 0x24, 0x76});
        check_eq(cpu.IX(), 0x2000, "INC IXH result");
        check_eq(cpu.F(), kY | kH, "INC IXH uses result X/Y");
    }

    {
        CPU cpu = run({0xFD, 0x21, 0x20, 0x20, 0xFD, 0x2E, 0x20, 0xFD, 0x2D, 0x76});
        check_eq(cpu.IY(), 0x201F, "DEC IYL result");
        check_eq(cpu.F(), kX | kH | kN, "DEC IYL uses result X/Y");
    }

    std::cout << "\n=========================\n";
    if (failures == 0) {
        std::cout << "ALL INC/DEC FLAG CHECKS PASSED\n";
        return 0;
    }
    std::cout << failures << " check(s) FAILED\n";
    return 1;
}
