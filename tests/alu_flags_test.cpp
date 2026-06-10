//
// Z80 Digital Twin - 8-bit ALU flag verification
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
constexpr uint8_t kZ = 0x40;
constexpr uint8_t kS = 0x80;

int failures = 0;

std::pair<uint8_t, uint8_t> run(const std::vector<uint8_t>& prog) {
    CPU cpu;
    cpu.LoadProgram(prog, 0x0000);
    for (int i = 0; i < 200 && !cpu.IsHalted(); ++i) {
        do {
            cpu.Step();
        } while (!cpu.InstructionComplete());
    }
    return {cpu.A(), cpu.F()};
}

void check_eq(uint8_t actual, uint8_t expected, const std::string& what) {
    std::cout << ((actual == expected) ? "  ok " : "  !! ") << what
              << " expected=0x" << std::hex << static_cast<int>(expected)
              << " actual=0x" << static_cast<int>(actual) << std::dec << '\n';
    if (actual != expected) ++failures;
}

void check_mask(uint8_t actual, uint8_t mask, uint8_t expected, const std::string& what) {
    check_eq(actual & mask, expected, what);
}

} // namespace

int main() {
    std::cout << "8-bit ALU flag verification\n===========================\n";

    {
        auto [a, f] = run({0x3E, 0x18, 0xC6, 0x08, 0x76}); // LD A,18h; ADD A,08h
        check_eq(a, 0x20, "ADD result");
        check_eq(f, kY | kH, "ADD sets result Y and half-carry");
    }

    {
        auto [a, f] = run({0x3E, 0x00, 0x37, 0xCE, 0xFF, 0x76}); // SCF; ADC A,FFh
        check_eq(a, 0x00, "ADC result with carry-in");
        check_eq(f, kZ | kH | kC, "ADC handles carry-in for H/C without stale X/Y");
    }

    {
        auto [a, f] = run({0x3E, 0x20, 0xD6, 0x01, 0x76}); // SUB 01h
        check_eq(a, 0x1F, "SUB result");
        check_eq(f, kN | kH | kX, "SUB sets result X and half-borrow");
    }

    {
        auto [a, f] = run({0x3E, 0x00, 0x37, 0xDE, 0x00, 0x76}); // SCF; SBC A,00h
        check_eq(a, 0xFF, "SBC result with borrow");
        check_eq(f, kS | kY | kH | kX | kN | kC, "SBC sets result S/X/Y/H/N/C");
    }

    {
        auto [a, f] = run({0x3E, 0x40, 0xFE, 0x28, 0x76}); // CP 28h
        check_eq(a, 0x40, "CP does not change A");
        check_eq(f, kY | kH | kX | kN, "CP takes X/Y from operand, not subtraction result");
    }

    {
        auto [a, f] = run({0x3E, 0x2A, 0xE6, 0x2A, 0x76}); // AND 2Ah
        check_eq(a, 0x2A, "AND result");
        check_eq(f, kY | kH | kX, "AND sets H and result X/Y");
    }

    {
        auto [a, f] = run({0x3E, 0x2A, 0xF6, 0x00, 0x76}); // OR 00h
        check_eq(a, 0x2A, "OR result");
        check_eq(f, kY | kX, "OR clears H and keeps result X/Y");
    }

    {
        auto [a, f] = run({0x3E, 0x2A, 0xEE, 0x00, 0x76}); // XOR 00h
        check_eq(a, 0x2A, "XOR result");
        check_eq(f, kY | kX, "XOR clears H and keeps result X/Y");
    }

    {
        auto [a, f] = run({0x3E, 0xD7, 0xB7, 0x2F, 0x76}); // OR A; CPL
        check_eq(a, 0x28, "CPL result");
        check_eq(f, kS | kY | kH | kX | kPV | kN, "CPL preserves S/Z/PV/C and sets H/N plus A X/Y");
    }

    {
        auto [a, f] = run({0x3E, 0x28, 0x37, 0x76}); // SCF
        check_eq(a, 0x28, "SCF leaves A");
        check_eq(f, kY | kX | kC, "SCF sets carry, clears H/N, updates X/Y from A");
    }

    {
        auto [a, f] = run({0x3E, 0x28, 0x37, 0x3F, 0x76}); // SCF; CCF
        check_eq(a, 0x28, "CCF leaves A");
        check_eq(f, kY | kH | kX, "CCF copies old carry to H, clears C/N, updates X/Y from A");
    }

    {
        auto [a, f] = run({0x3E, 0x15, 0xC6, 0x05, 0x27, 0x76}); // DAA -> 20h
        check_eq(a, 0x20, "DAA result");
        check_mask(f, kY | kX | kZ | kS, kY, "DAA recomputes visible result flags including X/Y");
    }

    std::cout << "\n===========================\n";
    if (failures == 0) {
        std::cout << "ALL ALU FLAG CHECKS PASSED\n";
        return 0;
    }
    std::cout << failures << " check(s) FAILED\n";
    return 1;
}
