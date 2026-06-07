//
// Z80 Digital Twin - maskable interrupt (CPU::Interrupt) verification
// Copyright (c) 2025-2026 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//
// Covers acceptance in IM0/1/2, masking via IFF1, HALT wake, the EI one-
// instruction deferral, and the pushed return address.
//

#include "z80_cpu.h"

#include <cstdint>
#include <iostream>
#include <vector>

namespace {

using z80::CPU;

int failures = 0;
void check(bool ok, const char* what) {
    std::cout << (ok ? "  ✓ " : "  ✗ ") << what << '\n';
    if (!ok) ++failures;
}

void step1(CPU& c) {  // one whole instruction (across prefix bytes)
    do { c.Step(); } while (!c.InstructionComplete());
}

void load(CPU& c, std::vector<uint8_t> bytes) {
    c.Reset();
    c.LoadProgram(bytes, 0x0000);
}

uint16_t stack_word(CPU& c) {  // the word at SP (a pushed return address)
    return static_cast<uint16_t>(c.ReadMemory(c.SP()) |
                                 (c.ReadMemory(static_cast<uint16_t>(c.SP() + 1)) << 8));
}

} // namespace

int main() {
    std::cout << "Maskable interrupt verification\n===============================\n";

    // --- IM 1 acceptance ----------------------------------------------------
    std::cout << "\n[1] IM 1: accept, push PC, jump to 0x0038\n";
    {
        CPU c;
        load(c, {0xED, 0x56});   // IM 1
        step1(c);                // set mode 1
        c.IFF1() = true; c.IFF2() = true;  // enabled, no EI shadow
        c.SP() = 0xFFF0;
        const uint16_t ret = c.PC();
        const uint64_t t0 = c.GetCycleCount();

        const bool accepted = c.Interrupt();
        check(accepted, "interrupt accepted");
        check(c.PC() == 0x0038, "PC = 0x0038");
        check(c.SP() == 0xFFEE, "SP decreased by 2");
        check(stack_word(c) == ret, "return address pushed");
        check(!c.IFF1() && !c.IFF2(), "IFF1/IFF2 cleared on acknowledge");
        check(c.GetCycleCount() - t0 == 13, "13 T-states");
    }

    // --- Masked when IFF1 clear --------------------------------------------
    std::cout << "\n[2] Masked when interrupts disabled\n";
    {
        CPU c;
        load(c, {0x00});   // NOP; IFF1 = false after reset
        const uint16_t pc = c.PC();
        const uint16_t sp = c.SP();
        check(!c.Interrupt(), "interrupt ignored (IFF1 = 0)");
        check(c.PC() == pc && c.SP() == sp, "no state change");
    }

    // --- EI one-instruction deferral ---------------------------------------
    std::cout << "\n[3] EI defers acceptance by one instruction\n";
    {
        CPU c;
        load(c, {0xED, 0x56, 0xFB, 0x00});  // IM 1 ; EI ; NOP
        step1(c);                            // IM 1
        step1(c);                            // EI -> IFF1=1 but deferred
        check(!c.Interrupt(), "declined immediately after EI");
        check(c.PC() != 0x0038, "did not jump");
        step1(c);                            // NOP -> deferral window closes
        check(c.Interrupt(), "accepted after the following instruction");
        check(c.PC() == 0x0038, "jumped to 0x0038");
    }

    // --- HALT wake ----------------------------------------------------------
    std::cout << "\n[4] Interrupt wakes a halted CPU\n";
    {
        CPU c;
        load(c, {0xED, 0x56, 0xFB, 0x00, 0x76});  // IM1 ; EI ; NOP ; HALT
        step1(c); step1(c); step1(c);              // through the NOP (defer clear)
        step1(c);                                  // HALT
        check(c.IsHalted(), "CPU halted");
        const uint16_t after_halt = c.PC();        // return address = past HALT
        check(c.Interrupt(), "interrupt accepted");
        check(!c.IsHalted(), "HALT woken");
        check(c.PC() == 0x0038, "jumped to handler");
        check(stack_word(c) == after_halt, "return address is the byte after HALT");
    }

    // --- IM 2 vectored ------------------------------------------------------
    std::cout << "\n[5] IM 2: [I:bus] vector\n";
    {
        CPU c;
        load(c, {0xED, 0x5E});   // IM 2
        step1(c);
        c.I() = 0x80;            // vector page
        c.IFF1() = true;
        c.WriteMemory(0x80FF, 0x34);   // vector at (0x80<<8)|0xFF -> 0x1234
        c.WriteMemory(0x8100, 0x12);
        const uint64_t t0 = c.GetCycleCount();
        check(c.Interrupt(0xFF), "interrupt accepted");
        check(c.PC() == 0x1234, "jumped via vector table to 0x1234");
        check(c.GetCycleCount() - t0 == 19, "19 T-states");
    }

    // --- IM 0 (RST from the bus) -------------------------------------------
    std::cout << "\n[6] IM 0: RST opcode on the bus\n";
    {
        CPU c;
        load(c, {0xED, 0x46});   // IM 0
        step1(c);
        c.IFF1() = true;
        check(c.Interrupt(0xFF), "accepted");
        check(c.PC() == 0x0038, "bus 0xFF (RST 38) -> 0x0038");

        CPU c2;
        load(c2, {0xED, 0x46});
        step1(c2);
        c2.IFF1() = true;
        c2.Interrupt(0xCF);      // RST 08
        check(c2.PC() == 0x0008, "bus 0xCF (RST 08) -> 0x0008");
    }

    std::cout << "\n===============================\n";
    if (failures == 0) {
        std::cout << "✅ ALL INTERRUPT CHECKS PASSED\n";
        return 0;
    }
    std::cout << "❌ " << failures << " check(s) FAILED\n";
    return 1;
}
