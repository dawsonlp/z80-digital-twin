//
// Z80 Digital Twin Debugger - DebugSession unit tests
// Copyright (c) 2025-2026 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//
// Verifies the debugger execution core: full-instruction stepping across a
// prefixed instruction, inline breakpoint stop/resume, write-watchpoints,
// dirty-cell tracking, budget-bounded slices, and HALT handling.
//

#include "debug_session.h"

#include <cstdint>
#include <iostream>
#include <vector>

namespace {

using namespace z80;
using namespace z80::dbg;

int failures = 0;
void check(bool ok, const char* what) {
    std::cout << (ok ? "  ✓ " : "  ✗ ") << what << '\n';
    if (!ok) ++failures;
}

// Program (loaded at 0x0000):
//   0x0000  3E 05        LD A, 0x05
//   0x0002  32 00 90     LD (0x9000), A
//   0x0005  CB 3F        SRL A            ; prefixed (CB) instruction
//   0x0007  00           NOP
//   0x0008  76           HALT
const std::vector<uint8_t> kProgram = {
    0x3E, 0x05,
    0x32, 0x00, 0x90,
    0xCB, 0x3F,
    0x00,
    0x76,
};

DebugCPU make_cpu() {
    DebugCPU cpu;
    cpu.LoadProgram(kProgram, 0x0000);
    return cpu;
}

} // namespace

int main() {
    std::cout << "DebugSession unit tests\n=======================\n";

    // --- Full-instruction stepping, including across a CB prefix ------------
    std::cout << "\n[1] StepInstruction advances one whole instruction\n";
    {
        DebugCPU cpu = make_cpu();
        DebugSession s(cpu);

        StepResult r1 = s.StepInstruction();             // LD A,0x05
        check(cpu.PC() == 0x0002, "PC = 0x0002 after LD A,n");
        check(cpu.A() == 0x05, "A = 0x05");
        check(r1.reason == StopReason::StepComplete, "reason StepComplete");

        s.StepInstruction();                             // LD (0x9000),A
        check(cpu.PC() == 0x0005, "PC = 0x0005 after LD (nn),A");

        // The CB-prefixed SRL A must complete in a single StepInstruction(),
        // i.e. PC moves 0x0005 -> 0x0007, not stalling on the prefix byte.
        s.StepInstruction();                             // SRL A  (0x05 -> 0x02)
        check(cpu.PC() == 0x0007, "PC = 0x0007 after CB-prefixed SRL A");
        check(cpu.A() == 0x02, "A = 0x02 after SRL A");
    }

    // --- Breakpoint stops a run, resume makes progress ----------------------
    std::cout << "\n[2] Breakpoint stop + resume\n";
    {
        DebugCPU cpu = make_cpu();
        DebugSession s(cpu);
        s.AddBreakpoint(0x0005);
        s.Run();

        StepResult r = s.RunSlice(1000);
        check(r.reason == StopReason::Breakpoint, "stopped on breakpoint");
        check(cpu.PC() == 0x0005, "paused at PC = 0x0005 (before executing)");
        check(s.State() == RunState::Paused, "state Paused");
        check(s.Breakpoints().at(0).hit_count == 1, "hit_count = 1");

        // Resume: must execute the instruction it is sitting on, not re-trigger.
        s.Run();
        StepResult r2 = s.RunSlice(1000);
        check(r2.reason == StopReason::Halted, "ran to HALT after resume");
        check(cpu.IsHalted(), "CPU halted");
        check(s.Breakpoints().at(0).hit_count == 1, "hit_count still 1 (no re-trigger)");
    }

    // --- Write-watchpoint stops a run --------------------------------------
    std::cout << "\n[3] Write-watchpoint\n";
    {
        DebugCPU cpu = make_cpu();
        DebugSession s(cpu);
        s.AddWatchpoint(0x9000);
        s.Run();

        StepResult r = s.RunSlice(1000);
        check(r.reason == StopReason::Watchpoint, "stopped on watchpoint");
        check(s.LastWatchpointHit().has_value() &&
              *s.LastWatchpointHit() == 0x9000, "watch hit at 0x9000");
        check(cpu.ReadMemory(0x9000) == 0x05, "value committed before stop");
        // The store is the 2nd instruction; PC should be just past it.
        check(cpu.PC() == 0x0005, "paused just after the store");
    }

    // --- Dirty-cell tracking ------------------------------------------------
    std::cout << "\n[4] Dirty-cell tracking\n";
    {
        DebugCPU cpu = make_cpu();
        DebugSession s(cpu);
        s.Run();
        s.RunSlice(1000);   // runs to HALT
        check(s.DirtyAddresses().count(0x9000) == 1, "0x9000 marked dirty");
        s.ClearDirty();
        check(s.DirtyAddresses().empty(), "dirty cleared");
    }

    // --- Budget-bounded slice keeps Running --------------------------------
    std::cout << "\n[5] Budget-bounded slice\n";
    {
        DebugCPU cpu = make_cpu();
        DebugSession s(cpu);
        s.Run();
        StepResult r = s.RunSlice(1);   // exactly one instruction
        check(r.reason == StopReason::BudgetExhausted, "budget exhausted");
        check(s.State() == RunState::Running, "still Running after partial slice");
        check(cpu.PC() == 0x0002, "advanced exactly one instruction");
    }

    // --- HALT handling ------------------------------------------------------
    std::cout << "\n[6] HALT handling\n";
    {
        DebugCPU cpu = make_cpu();
        DebugSession s(cpu);
        s.Run();
        s.RunSlice(1000);
        check(s.State() == RunState::Halted, "state Halted after HALT");
        StepResult r = s.StepInstruction();
        check(r.reason == StopReason::AlreadyHalted, "step is a no-op when halted");
        check(r.cycles == 0, "no cycles consumed when halted");

        s.Reset();
        check(s.State() == RunState::Paused, "Reset -> Paused");
        check(cpu.PC() == 0x0000, "Reset -> PC 0x0000");
    }

    // --- Step-Over ----------------------------------------------------------
    std::cout << "\n[7] Step-Over\n";
    {
        // 0x0000 CD 06 00  CALL 0x0006
        // 0x0003 3E 99     LD A, 0x99       (after return)
        // 0x0005 76        HALT
        // 0x0006 3E 11     LD A, 0x11       (subroutine)
        // 0x0008 C9        RET
        const std::vector<uint8_t> prog = {
            0xCD, 0x06, 0x00, 0x3E, 0x99, 0x76, 0x3E, 0x11, 0xC9};
        DebugCPU cpu;
        cpu.LoadProgram(prog, 0x0000);
        DebugSession s(cpu);

        StepResult r = s.StepOver();                  // over the CALL
        check(r.reason == StopReason::StepComplete, "step-over CALL completes");
        check(cpu.PC() == 0x0003, "returned to 0x0003");
        check(cpu.A() == 0x11, "subroutine ran (A = 0x11)");

        StepResult r2 = s.StepOver();                 // non-call -> single step
        check(r2.reason == StopReason::StepComplete, "step-over non-call steps");
        check(cpu.PC() == 0x0005, "advanced to HALT");
        check(cpu.A() == 0x99, "executed LD A,0x99");
    }

    // Step-Over stops at a user breakpoint inside the subroutine.
    {
        const std::vector<uint8_t> prog = {
            0xCD, 0x06, 0x00, 0x3E, 0x99, 0x76, 0x3E, 0x11, 0xC9};
        DebugCPU cpu;
        cpu.LoadProgram(prog, 0x0000);
        DebugSession s(cpu);
        s.AddBreakpoint(0x0006);                      // inside the subroutine

        StepResult r = s.StepOver();
        check(r.reason == StopReason::Breakpoint, "user BP inside sub stops step-over");
        check(cpu.PC() == 0x0006, "paused at the inner breakpoint");
        check(s.HasBreakpoint(0x0003) == false, "temp return BP cleaned up");
    }

    // Step-Over on a NOT-taken conditional CALL falls through (no subroutine).
    {
        // 0x0000 AF        XOR A            (Z = 1)
        // 0x0001 C4 08 00  CALL NZ, 0x0008  (NZ false -> not taken)
        // 0x0004 3E 22     LD A, 0x22
        // 0x0006 76        HALT
        // 0x0007 00        NOP
        // 0x0008 3E 55     LD A, 0x55       (must NOT run)
        // 0x000A C9        RET
        const std::vector<uint8_t> prog = {
            0xAF, 0xC4, 0x08, 0x00, 0x3E, 0x22, 0x76, 0x00, 0x3E, 0x55, 0xC9};
        DebugCPU cpu;
        cpu.LoadProgram(prog, 0x0000);
        DebugSession s(cpu);

        s.StepInstruction();                          // XOR A -> Z=1
        check(cpu.PC() == 0x0001, "at the conditional CALL");
        StepResult r = s.StepOver();                  // not taken: fall through
        check(r.reason == StopReason::StepComplete, "not-taken CALL falls through");
        check(cpu.PC() == 0x0004, "advanced past the CALL to 0x0004");
        check(cpu.A() == 0x00, "subroutine did not run (A still 0)");
    }

    // --- Execution coverage (L1) --------------------------------------------
    std::cout << "\n[8] Execution coverage map\n";
    {
        DebugCPU cpu = make_cpu();   // GCD prog: LD A,5; LD (0x9000),A; SRL A; NOP; HALT
        DebugSession s(cpu);
        check(s.CoveredBytes() == 0, "no coverage before running");

        s.Run();
        s.RunSlice(1000);   // runs to HALT

        check((s.CoverageFlags(0x0000) & kExecOpcode) != 0, "0x0000 marked opcode");
        // LD A,0x05 is 2 bytes; 0x0001 is its operand.
        check((s.CoverageFlags(0x0001) & kExecOperand) != 0, "0x0001 marked operand");
        // CB-prefixed SRL A at 0x0005 spans 2 bytes (CB 3F).
        check((s.CoverageFlags(0x0005) & kExecOpcode) != 0, "0x0005 marked opcode");
        check((s.CoverageFlags(0x0006) & kExecOperand) != 0, "0x0006 (CB subop) marked");
        check(s.CoverageFlags(0x4000) == 0, "untouched address has no coverage");
        check(s.CoveredBytes() >= 9, "covered byte count is plausible");

        s.Reset();
        check(s.CoveredBytes() == 0, "Reset clears coverage");
    }

    // --- Self-modifying code detection (L2) ---------------------------------
    std::cout << "\n[9] Self-modifying code\n";
    {
        // Self-incrementing operand loop:
        //   0x0000 3E 00     LD A, 0x00     (operand 0x0001 gets incremented)
        //   0x0002 21 01 00  LD HL, 0x0001
        //   0x0005 34        INC (HL)       -> writes 0x0001 (executed operand = SMC)
        //   0x0006 18 F8     JR 0x0000
        const std::vector<uint8_t> prog = {
            0x3E, 0x00, 0x21, 0x01, 0x00, 0x34, 0x18, 0xF8};
        DebugCPU cpu;
        cpu.LoadProgram(prog, 0x0000);
        DebugSession s(cpu);

        check(s.SmcCount() == 0, "no SMC before running");
        s.Run();
        s.RunSlice(8);   // ~2 loops

        check(s.SmcCount() >= 1, "SMC detected");
        check((s.CoverageFlags(0x0001) & kSelfModified) != 0, "0x0001 flagged self-modified");
        const auto& ev = s.SmcEvents();
        check(!ev.empty() && ev[0].address == 0x0001, "event target is 0x0001");
        check(!ev.empty() && ev[0].writer_pc == 0x0005, "writer PC is the INC (HL)");
        check(!ev.empty() && ev[0].old_value == 0x00 && ev[0].new_value == 0x01,
              "captured old=0x00 new=0x01");

        // A write to a never-executed data address is not SMC.
        const uint64_t before = s.SmcCount();
        cpu.WriteMemory(0x9000, 0x42);
        check(s.SmcCount() == before, "write to non-code is not SMC");

        // Break-on-SMC stops a run at the offending write.
        DebugCPU cpu2;
        cpu2.LoadProgram(prog, 0x0000);
        DebugSession s2(cpu2);
        s2.SetBreakOnSmc(true);
        s2.Run();
        StepResult r = s2.RunSlice(1000);
        check(r.reason == StopReason::SelfModified, "Break-on-SMC stops the run");
        check(s2.State() == RunState::Paused, "paused after SMC break");
    }

    // --- T-state-budgeted run (the machine frame primitive) -----------------
    std::cout << "\n[10] RunForTStates (frame primitive)\n";
    {
        // Spin forever: JR $  (12 T-states per iteration).
        DebugCPU cpu;
        cpu.LoadProgram({0x18, 0xFE}, 0x0000);
        DebugSession s(cpu);
        s.Run();
        StepResult r = s.RunForTStates(500);
        check(r.reason == StopReason::BudgetExhausted, "stops once the T-state budget is met");
        check(r.cycles >= 500, "ran at least the budget");
        check(r.cycles < 500 + 16, "overran by at most one instruction");
        check(s.State() == RunState::Running, "still Running after a full frame");
    }
    {
        // HALT ends a frame early.
        DebugCPU cpu = make_cpu();
        DebugSession s(cpu);
        s.Run();
        StepResult r = s.RunForTStates(1'000'000);
        check(r.reason == StopReason::Halted, "HALT stops a frame before the budget");
        check(s.State() == RunState::Halted, "state Halted");
    }
    {
        // A breakpoint stops a frame early (and only burns the elapsed T-states).
        DebugCPU cpu;
        cpu.LoadProgram({0x18, 0xFE}, 0x0000);   // JR $
        DebugSession s(cpu);
        s.AddBreakpoint(0x0000);
        s.Run();
        StepResult r = s.RunForTStates(1'000'000);
        check(r.reason == StopReason::Breakpoint, "breakpoint stops a frame");
        check(cpu.PC() == 0x0000, "paused at the breakpoint");
        check(r.cycles < 1'000'000, "did not burn the whole budget");
    }

    // --- Blocked writes to write-protected memory (not SMC) -----------------
    std::cout << "\n[11] Blocked writes to write-protected memory\n";
    {
        // Code in RAM writes to a protected low address.
        //   0x8000 3E AA     LD A, 0xAA
        //   0x8002 32 00 00  LD (0x0000), A   ; protected -> refused
        //   0x8005 76        HALT
        DebugCPU cpu;
        cpu.GetMemory()[0x0000] = 0x11;            // seed (still writable)
        cpu.LoadProgram({0x3E, 0xAA, 0x32, 0x00, 0x00, 0x76}, 0x8000);
        cpu.PC() = 0x8000;
        DebugSession s(cpu);
        cpu.GetMemory().SetWriteProtect(0x0000, 0x00FF);

        s.Run();
        s.RunSlice(100);   // runs to HALT

        check(cpu.ReadMemory(0x0000) == 0x11, "protected byte unchanged");
        check(s.BlockedWriteCount() == 1, "one blocked write recorded");
        check(s.SmcCount() == 0, "blocked write is NOT counted as SMC");
        check((s.CoverageFlags(0x0000) & kBlockedWrite) != 0, "0x0000 flagged kBlockedWrite");
        check((s.CoverageFlags(0x0000) & kSelfModified) == 0, "0x0000 NOT flagged kSelfModified");
        const auto& bw = s.BlockedWrites();
        check(!bw.empty() && bw[0].address == 0x0000, "event target 0x0000");
        check(!bw.empty() && bw[0].attempted_value == 0xAA && bw[0].current_value == 0x11,
              "event records attempted 0xAA, current 0x11");
        check(!bw.empty() && bw[0].writer_pc == 0x8002, "writer PC is the LD (nn),A");
    }

    std::cout << "\n=======================\n";
    if (failures == 0) {
        std::cout << "✅ ALL DEBUG-SESSION CHECKS PASSED\n";
        return 0;
    }
    std::cout << "❌ " << failures << " check(s) FAILED\n";
    return 1;
}
