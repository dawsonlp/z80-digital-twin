//
// Z80 Digital Twin - register aliasing contract verification
// Copyright (c) 2025-2026 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//

#include "z80_cpu.h"

#include <bit>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

using z80::CPU;

int failures = 0;

void step_instruction(CPU& cpu) {
    do {
        cpu.Step();
    } while (!cpu.InstructionComplete());
}

void run_program(CPU& cpu, const std::vector<uint8_t>& program) {
    cpu.LoadProgram(program, 0x0000);
    cpu.PC() = 0;
    for (int i = 0; i < 20 && !cpu.IsHalted(); ++i) {
        step_instruction(cpu);
    }
}

void check_eq(uint16_t actual, uint16_t expected, const std::string& what) {
    std::cout << ((actual == expected) ? "  ok " : "  !! ") << what
              << " expected=0x" << std::hex << expected
              << " actual=0x" << actual << std::dec << '\n';
    if (actual != expected) ++failures;
}

void explain_aliasing_contract() {
    std::cout
        << "\nRegister aliasing contract:\n"
        << "  The CPU register file intentionally models each Z80 register pair\n"
        << "  as shared 16-bit and 8-bit storage. This requires the host layout\n"
        << "  used by RegisterPair to map byte 0 to the Z80 low byte and byte 1\n"
        << "  to the Z80 high byte. On a big-endian host this test is expected\n"
        << "  to fail: B/C, D/E, H/L, IXH/IXL, and IYH/IYL no longer alias the\n"
        << "  same way a Z80 register pair behaves.\n";
}

} // namespace

int main() {
    std::cout << "Register aliasing verification\n==============================\n";

    if constexpr (std::endian::native == std::endian::big) {
        std::cout << "Host endian: big\n";
    } else if constexpr (std::endian::native == std::endian::little) {
        std::cout << "Host endian: little\n";
    } else {
        std::cout << "Host endian: mixed/unknown\n";
    }

    {
        CPU cpu;
        run_program(cpu, {0x01, 0x34, 0x12, 0x76}); // LD BC,1234h; HALT

        check_eq(cpu.BC(), 0x1234, "LD BC,nn loads the 16-bit register pair");
        check_eq(cpu.B(), 0x12, "LD BC,1234h makes B the high byte");
        check_eq(cpu.C(), 0x34, "LD BC,1234h makes C the low byte");
    }

    {
        CPU cpu;
        run_program(cpu, {0x06, 0x12, 0x0E, 0x34, 0x03, 0x76});
        // LD B,12h; LD C,34h; INC BC; HALT

        check_eq(cpu.BC(), 0x1235, "8-bit B/C writes alias the 16-bit BC register");
        check_eq(cpu.B(), 0x12, "INC BC preserves B when low byte does not carry");
        check_eq(cpu.C(), 0x35, "INC BC increments C as BC's low byte");
    }

    {
        CPU cpu;
        run_program(cpu, {0xDD, 0x21, 0x34, 0x12, 0xDD, 0x24, 0x76});
        // LD IX,1234h; INC IXH; HALT

        check_eq(cpu.IX(), 0x1334, "IXH aliases the high byte of IX");
    }

    std::cout << "\n==============================\n";
    if (failures == 0) {
        std::cout << "ALL REGISTER ALIASING CHECKS PASSED\n";
        return 0;
    }

    explain_aliasing_contract();
    std::cout << failures << " check(s) FAILED\n";
    return 1;
}
