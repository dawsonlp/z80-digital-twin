//
// Z80 Digital Twin - I/O policy verification
// Copyright (c) 2025 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//
// Verifies the I/O compile-time policy:
//   1. OpenBusIo  — Out discarded, In returns 0xFF (no storage).
//   2. LatchedIo  — round-trips; high byte ignored; Peek/Poke.
//   3. ObservableIo<Inner> — forwards to inner, logs every transaction.
//   4. Integration — the CPU drives the full 16-bit port to the device:
//      OUT (n),A and IN A,(n) put A on the high byte; (C) forms use BC.
//

#include "z80_cpu.h"
#include "io/open_bus_io.h"
#include "io/latched_io.h"
#include "io/observable_io.h"
#include "memory/observable_memory.h"

#include <cstdint>
#include <iostream>
#include <vector>

namespace {

using namespace z80;

int failures = 0;
void check(bool ok, const char* what) {
    std::cout << (ok ? "  ✓ " : "  ✗ ") << what << '\n';
    if (!ok) ++failures;
}

} // namespace

int main() {
    std::cout << "I/O policy verification\n=======================\n";

    // --- 1. OpenBusIo -------------------------------------------------------
    std::cout << "\n[1] OpenBusIo (nothing attached)\n";
    {
        OpenBusIo io;
        io.Out(0x00FE, 0x42);                 // discarded
        check(io.In(0x00FE) == 0xFF, "In returns 0xFF (floating)");
        check(io.In(0x1234) == 0xFF, "any port reads 0xFF");
    }

    // --- 2. LatchedIo ------------------------------------------------------
    std::cout << "\n[2] LatchedIo (256 latches, low byte)\n";
    {
        LatchedIo io;
        io.Out(0x00FE, 0x42);
        check(io.In(0x00FE) == 0x42, "round-trips a written port");
        check(io.In(0xABFE) == 0x42, "high byte ignored (0xABFE == 0x00FE)");
        io.Poke(0x10, 0x99);
        check(io.Peek(0x10) == 0x99, "Peek/Poke");
    }

    // --- 3. ObservableIo decorator -----------------------------------------
    std::cout << "\n[3] ObservableIo<LatchedIo> (forwards + logs)\n";
    {
        ObservableIo<LatchedIo> io;
        io.Out(0x1234, 0x5A);
        const uint8_t v = io.In(0x0034);      // low byte 0x34 -> same latch
        check(v == 0x5A, "forwards to inner device (round-trip via inner)");
        check(io.inner().Peek(0x34) == 0x5A, "inner() exposes the device");

        const auto& log = io.Transactions();
        check(log.size() == 2, "two transactions logged");
        check(log[0].is_out && log[0].port == 0x1234 && log[0].value == 0x5A,
              "transaction 0 = OUT 0x1234 = 0x5A");
        check(!log[1].is_out && log[1].port == 0x0034 && log[1].value == 0x5A,
              "transaction 1 = IN 0x0034 -> 0x5A");
        check(io.TransactionCount() == 2, "transaction count");
        io.ClearTransactions();
        check(io.Transactions().empty() && io.TransactionCount() == 2,
              "ClearTransactions clears log, total persists");
    }

    // --- 4. CPU integration: full 16-bit port reaches the device -----------
    std::cout << "\n[4] CPU passes the full 16-bit port\n";
    {
        // 0x00 3E 5A     LD A, 0x5A
        // 0x02 D3 FE     OUT (0xFE), A     ; port = 0x5AFE (A on high byte)
        // 0x04 3E AB     LD A, 0xAB
        // 0x06 DB FE     IN A, (0xFE)      ; port = 0xABFE (A on high byte)
        // 0x08 01 34 12  LD BC, 0x1234
        // 0x0B ED 79     OUT (C), A        ; port = BC = 0x1234
        // 0x0D 76        HALT
        const std::vector<uint8_t> prog = {
            0x3E, 0x5A, 0xD3, 0xFE, 0x3E, 0xAB, 0xDB, 0xFE,
            0x01, 0x34, 0x12, 0xED, 0x79, 0x76};

        CPUImpl<ObservableMemory, ObservableIo<LatchedIo>> cpu;
        cpu.LoadProgram(prog, 0x0000);
        for (int g = 0; g < 1000 && !cpu.IsHalted(); ++g) {
            do { cpu.Step(); } while (!cpu.InstructionComplete());
        }
        check(cpu.IsHalted(), "program halted");

        const auto& log = cpu.GetIo().Transactions();
        check(log.size() == 3, "three I/O transactions");
        check(log[0].is_out && log[0].port == 0x5AFE && log[0].value == 0x5A,
              "OUT (n),A -> port 0x5AFE (A high byte)");
        check(!log[1].is_out && log[1].port == 0xABFE,
              "IN A,(n) -> port 0xABFE (A high byte)");
        check(log[2].is_out && log[2].port == 0x1234,
              "OUT (C),A -> port 0x1234 (BC)");
    }

    std::cout << "\n=======================\n";
    if (failures == 0) {
        std::cout << "✅ ALL I/O-POLICY CHECKS PASSED\n";
        return 0;
    }
    std::cout << "❌ " << failures << " check(s) FAILED\n";
    return 1;
}
