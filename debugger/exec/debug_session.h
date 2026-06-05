//
// Z80 Digital Twin Debugger - DebugSession
// Copyright (c) 2025 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//
// DebugSession owns the debugger's execution loop. It drives a DebugCPU one
// whole instruction at a time, runs bounded slices with inline breakpoint
// checks, and surfaces memory-write events (dirty cells + write-watchpoints)
// via the DebugMemory hook. It holds no UI state; a UI layer calls into it and
// reads CPU state through it.
//
// Lifetime/RAII: the session installs a write hook capturing `this` for its
// whole lifetime and clears it in the destructor, so no dangling callback can
// outlive the session. The referenced DebugCPU must outlive the session.
//

#ifndef Z80_DBG_DEBUG_SESSION_H
#define Z80_DBG_DEBUG_SESSION_H

#include "z80_cpu.h"
#include "memory/debug_memory.h"
#include "disassembler.h"

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace z80::dbg {

/// @brief The CPU configuration the debugger drives: hooked memory plug.
using DebugCPU = CPUImpl<DebugMemory>;

/// @brief High-level run state of the session.
enum class RunState {
    Paused,   ///< Not executing; waiting for a command.
    Running,  ///< Free-running in bounded slices.
    Halted,   ///< CPU executed HALT; nothing left to run.
};

/// @brief Why the most recent execution action stopped.
enum class StopReason {
    StepComplete,      ///< A single-instruction step finished.
    Breakpoint,        ///< Execution paused at an enabled breakpoint.
    Watchpoint,        ///< A watched memory address was written.
    Halted,            ///< CPU reached HALT.
    BudgetExhausted,   ///< Run slice used its full instruction budget.
    AlreadyHalted,     ///< Action requested while already halted (no-op).
};

/// @brief A program-counter breakpoint with metadata.
struct Breakpoint {
    uint16_t address = 0;
    bool enabled = true;
    bool temporary = false;   ///< Auto-removed once hit (e.g. step-over return).
    uint64_t hit_count = 0;
};

/// @brief Outcome of a step or run-slice action.
struct StepResult {
    StopReason reason = StopReason::StepComplete;
    uint64_t cycles = 0;   ///< T-states consumed by this action.
    uint16_t pc = 0;       ///< PC after the action.
};

class DebugSession {
public:
    /// @brief Construct around a CPU and install the memory write hook.
    explicit DebugSession(DebugCPU& cpu);

    /// @brief Remove the installed write hook.
    ~DebugSession();

    DebugSession(const DebugSession&) = delete;
    DebugSession& operator=(const DebugSession&) = delete;

    // -- Execution control ---------------------------------------------------

    /// @brief Advance exactly one whole instruction (across any prefix bytes).
    StepResult StepInstruction();

    /// @brief Step a whole instruction, but run CALL/RST subroutines to
    ///        completion (stopping at the instruction after the call).
    /// @details Decodes the instruction at PC for its length; for CALL/RST it
    ///          sets a temporary breakpoint at the return address and runs until
    ///          it is reached (handling not-taken conditional calls, which fall
    ///          through immediately). Any other instruction behaves like
    ///          StepInstruction(). A user breakpoint hit inside the subroutine
    ///          stops early and is reported as Breakpoint. Recursion stops at
    ///          the first return to that address (a known simple-step-over
    ///          limitation). Runs synchronously.
    StepResult StepOver();

    /// @brief Mark the session running so RunSlice() will execute.
    void Run() noexcept { if (state_ != RunState::Halted) state_ = RunState::Running; }

    /// @brief Stop free-running. Safe to call between slices.
    void Pause() noexcept { if (state_ != RunState::Halted) state_ = RunState::Paused; }

    /// @brief Execute up to @p max_instructions, stopping early on a breakpoint,
    ///        watchpoint, or HALT. Only runs when State() == Running.
    /// @details On a breakpoint/watchpoint/HALT the state becomes Paused/Halted;
    ///          if the budget is exhausted the state stays Running so the caller
    ///          continues next frame. Resuming from a breakpoint executes the
    ///          instruction it sits on before re-checking, so it makes progress.
    StepResult RunSlice(uint64_t max_instructions);

    /// @brief Reset the CPU and pause. Breakpoints and watchpoints are kept.
    void Reset();

    // -- Breakpoints ---------------------------------------------------------

    void AddBreakpoint(uint16_t address, bool temporary = false);
    void RemoveBreakpoint(uint16_t address);
    void ToggleBreakpoint(uint16_t address);
    void ClearBreakpoints() noexcept { breakpoints_.clear(); skip_breakpoint_once_.reset(); }
    [[nodiscard]] bool HasBreakpoint(uint16_t address) const;
    [[nodiscard]] std::vector<Breakpoint> Breakpoints() const;

    // -- Write watchpoints (powered by the DebugMemory hook) -----------------

    void AddWatchpoint(uint16_t address) { watchpoints_.insert(address); }
    void RemoveWatchpoint(uint16_t address) { watchpoints_.erase(address); }
    void ClearWatchpoints() noexcept { watchpoints_.clear(); }
    [[nodiscard]] std::vector<uint16_t> Watchpoints() const;

    /// @brief Address of the most recent watchpoint hit (if any since cleared).
    [[nodiscard]] std::optional<uint16_t> LastWatchpointHit() const noexcept {
        return watch_hit_;
    }

    // -- Memory-write observation (for UI change-highlighting) ---------------

    /// @brief Set of addresses written since the last ClearDirty().
    [[nodiscard]] const std::unordered_set<uint16_t>& DirtyAddresses() const noexcept {
        return dirty_;
    }
    void ClearDirty() noexcept { dirty_.clear(); }

    // -- State accessors -----------------------------------------------------

    [[nodiscard]] RunState State() const noexcept { return state_; }
    [[nodiscard]] DebugCPU& Cpu() noexcept { return cpu_; }
    [[nodiscard]] const DebugCPU& Cpu() const noexcept { return cpu_; }

private:
    /// @brief Run Step() until the CPU is at an instruction boundary.
    void StepRaw();

    /// @brief Whether an enabled breakpoint exists at @p pc.
    [[nodiscard]] bool BreakpointStopsAt(uint16_t pc) const;

    /// @brief Hook target: record dirty cell and detect watchpoint hits.
    void OnMemoryWrite(uint16_t address, uint8_t old_value, uint8_t new_value);

    // Bound generously above the longest Z80 prefix chain (DD CB d op = 3
    // Step() calls); guards against a non-advancing step loop.
    static constexpr int kStepByteGuard = 8;

    DebugCPU& cpu_;
    Disassembler disasm_;
    ByteReader reader_;            ///< Reads CPU memory for the disassembler.

    RunState state_ = RunState::Paused;

    std::unordered_map<uint16_t, Breakpoint> breakpoints_;
    std::unordered_set<uint16_t> watchpoints_;
    std::unordered_set<uint16_t> dirty_;

    // When resuming, the breakpoint we are sitting on is skipped for exactly one
    // instruction so execution makes progress instead of re-triggering.
    std::optional<uint16_t> skip_breakpoint_once_;

    // Set by the write hook when a watched address is written during a slice.
    std::optional<uint16_t> watch_hit_;
};

} // namespace z80::dbg

#endif // Z80_DBG_DEBUG_SESSION_H
