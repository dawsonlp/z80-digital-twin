//
// Z80 Digital Twin Debugger - DebugSession
// Copyright (c) 2025-2026 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//
// DebugSession owns the debugger's execution loop. It drives a DebugCPU one
// whole instruction at a time, runs bounded slices with inline breakpoint
// checks, and surfaces memory-write events (dirty cells + write-watchpoints)
// via the MetadataMemory write observers. It holds no UI state; a UI layer
// calls into it and reads CPU state through it.
//
// Lifetime/RAII: the session installs a write hook capturing `this` for its
// whole lifetime and clears it in the destructor, so no dangling callback can
// outlive the session. The referenced DebugCPU must outlive the session.
//

#ifndef Z80_DBG_DEBUG_SESSION_H
#define Z80_DBG_DEBUG_SESSION_H

#include "z80_cpu.h"
#include "memory/metadata_memory.h"
#include "io/latched_io.h"
#include "io/observable_io.h"
#include "io/callback_io.h"
#include "disassembler.h"
#include "instruction_history.h"

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace z80::dbg {

/// @brief The CPU configuration the debugger drives: observable memory + an
///        observable I/O device wrapping a CallbackIo. The transaction log feeds
///        the I/O panel; the inner CallbackIo lets a machine (e.g. the ZX
///        Spectrum ULA) hook its ports. With no handler installed the ports read
///        as an open bus. This is the *same* config the DebugSpectrumMachine uses, so
///        a DebugSession can drive a running Spectrum directly.
using DebugCPU = CPUImpl<MetadataMemory, ObservableIo<CallbackIo>>;

/// @brief High-level run state of the session.
enum class RunState {
    Paused,   ///< Not executing; waiting for a command.
    Running,  ///< Free-running in bounded slices.
    Halted,   ///< CPU halted; an attached machine may subsequently wake it.
};

/// @brief Why the most recent execution action stopped.
enum class StopReason {
    StepComplete,      ///< A single-instruction step finished.
    Breakpoint,        ///< Execution paused at an enabled breakpoint.
    Watchpoint,        ///< A watched memory address was written.
    Halted,            ///< CPU reached HALT.
    BudgetExhausted,   ///< Run slice used its full instruction budget.
    AlreadyHalted,     ///< Action requested while already halted (no-op).
    IncompleteInstruction, ///< Prefix-stage budget exhausted; execution can continue.
    SelfModified,      ///< Paused because Break-on-SMC was armed and code was written.
};

/// @brief Per-address execution/coverage flags (bitmask).
enum CoverageFlag : uint8_t {
    kExecOpcode   = 1u << 0,  ///< Was executed as an instruction's first byte.
    kExecOperand  = 1u << 1,  ///< Decoder-derived span of a completed instruction; not observed fetches.
    kSelfModified = 1u << 2,  ///< Written after having executed as code (SMC).
    kBlockedWrite = 1u << 3,  ///< A write here was refused (write-protected, e.g. ROM).
};

/// @brief A recorded self-modifying-code event.
struct SmcEvent {
    uint16_t address = 0;     ///< Code byte that was overwritten.
    uint8_t old_value = 0;    ///< Byte before the write (free, from the hook).
    uint8_t new_value = 0;    ///< Byte written.
    uint16_t writer_pc = 0;   ///< Instruction that performed the write.
    uint64_t cycle = 0;       ///< T-state count when it happened.
};

/// @brief A refused write to write-protected memory (e.g. ROM). Looks like SMC
///        but isn't: the byte is unchanged because a real bus ignores the write.
struct BlockedWrite {
    uint16_t address = 0;         ///< Protected address that was written to.
    uint8_t current_value = 0;    ///< The byte, unchanged (read-only).
    uint8_t attempted_value = 0;  ///< What the CPU tried to write.
    uint16_t writer_pc = 0;       ///< Instruction that attempted the write.
    uint64_t cycle = 0;           ///< T-state count when it happened.
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

    /// @brief Advance/continue one instruction; at most 4096 stages per action.
    ///        Report prefix-budget exhaustion explicitly; retain continuation state.
    StepResult StepInstruction(uint32_t stage_budget = 4096);

    // Machine lifecycle hooks surround execution, never UI refresh or pause.
    // Preparation may deliver a machine interrupt before breakpoint evaluation.
    void SetExecutionHooks(std::function<void()> prepare, std::function<void()> advance) {
        prepare_execution_ = std::move(prepare);
        advance_execution_ = std::move(advance);
    }

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
    ///        watchpoint, HALT or incomplete instruction. Starts Running.
    /// @details On a breakpoint/watchpoint/HALT the state becomes Paused/Halted;
    ///          if the budget is exhausted the state stays Running so the caller
    ///          continues next frame. Resuming from a breakpoint executes the
    ///          instruction it sits on before re-checking, so it makes progress.
    StepResult RunSlice(uint64_t max_instructions);

    /// @brief Execute whole instructions until at least @p tstate_budget T-states
    ///        have elapsed, stopping early on a breakpoint, watchpoint, SMC break,
    ///        or HALT. Sets itself Running; only stops free-run on those events.
    /// @details The breakpoint-aware frame primitive for a running machine: a PAL
    ///          frame is a fixed T-state budget, not an instruction count. The
    ///          final instruction may overrun the budget slightly (returned in
    ///          StepResult::cycles), so the caller can carry the remainder.
    ///          Resuming from a breakpoint steps past it once (as RunSlice does).
    StepResult RunForTStates(uint64_t tstate_budget);

    /// @brief Reset the CPU and pause. Breakpoints and watchpoints are kept.
    void Reset();

    // -- Breakpoints ---------------------------------------------------------

    void AddBreakpoint(uint16_t address, bool temporary = false);
    void RemoveBreakpoint(uint16_t address);
    void ToggleBreakpoint(uint16_t address);
    void ClearBreakpoints() noexcept { breakpoints_.clear(); skip_breakpoint_once_.reset(); }
    [[nodiscard]] bool HasBreakpoint(uint16_t address) const;
    [[nodiscard]] std::vector<Breakpoint> Breakpoints() const;

    // -- Write watchpoints (powered by a MetadataMemory observer) ---------

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

    [[nodiscard]] const InstructionHistory& History() const { return history_; }

    // -- Execution coverage (L1) ---------------------------------------------

    /// @brief Coverage flags for an address (see CoverageFlag).
    [[nodiscard]] uint8_t CoverageFlags(uint16_t address) const { return coverage_[address]; }

    /// @brief Number of distinct bytes seen as code (opcode or operand).
    [[nodiscard]] uint32_t CoveredBytes() const noexcept { return covered_bytes_; }

    /// @brief Coverage as a percentage of the 64 KB space.
    [[nodiscard]] double CoveragePercent() const noexcept {
        return 100.0 * static_cast<double>(covered_bytes_) / 65536.0;
    }

    // -- Self-modifying code (L2) --------------------------------------------

    /// @brief Recorded SMC events (capped; SmcCount() is the true total).
    [[nodiscard]] const std::vector<SmcEvent>& SmcEvents() const noexcept { return smc_events_; }

    /// @brief Total SMC writes detected (may exceed SmcEvents().size()).
    [[nodiscard]] uint64_t SmcCount() const noexcept { return smc_total_; }

    // -- Blocked writes (refused writes to write-protected memory, e.g. ROM) --

    /// @brief Recorded blocked-write attempts (capped; BlockedWriteCount() total).
    [[nodiscard]] const std::vector<BlockedWrite>& BlockedWrites() const noexcept {
        return blocked_writes_;
    }
    /// @brief Total refused writes to protected memory.
    [[nodiscard]] uint64_t BlockedWriteCount() const noexcept { return blocked_total_; }

    /// @brief Pause the run when code is overwritten.
    void SetBreakOnSmc(bool on) noexcept { break_on_smc_ = on; }
    [[nodiscard]] bool BreakOnSmc() const noexcept { return break_on_smc_; }

    // A machine may wake a halted CPU through its ordinary hardware lifecycle.
    [[nodiscard]] bool CanAdvance() const { return !cpu_.IsHalted() || bool(prepare_execution_); }

    // -- State accessors -----------------------------------------------------

    [[nodiscard]] RunState State() const noexcept { return state_; }
    [[nodiscard]] DebugCPU& Cpu() noexcept { return cpu_; }
    [[nodiscard]] const DebugCPU& Cpu() const noexcept { return cpu_; }

private:
    /// @brief Preserve writer context; publish coverage only on completion.
    bool ExecuteOneInstruction(uint32_t stage_budget = 4096);

    /// @brief Let the attached machine deliver events before executing/checking PC.
    void PrepareExecution();

    /// @brief Record a completed start and its separately decoded pre-execution span.
    void RecordCoverage(uint16_t start, uint32_t decoded_length);

    /// @brief Whether an enabled breakpoint exists at @p pc.
    [[nodiscard]] bool BreakpointStopsAt(uint16_t pc) const;

    /// @brief Hook target: record dirty cell and detect watchpoint hits.
    void OnMemoryWrite(uint16_t address, uint8_t old_value, uint8_t new_value);

    /// @brief Hook target: a write to write-protected memory was refused.
    void OnBlockedWrite(uint16_t address, uint8_t current_value, uint8_t attempted_value);

    std::function<void()> prepare_execution_, advance_execution_;
    bool instruction_pending_ = false;
    uint32_t pending_decoded_length_ = 0;
    uint64_t instruction_start_cycle_ = 0;
    InstructionHistory history_;

    DebugCPU& cpu_;
    Disassembler disasm_;
    ByteReader reader_;            ///< Reads CPU memory for the disassembler.
    int write_observer_id_ = -1;   ///< Our registration on the memory plug.
    int blocked_observer_id_ = -1; ///< Our blocked-write registration.

    RunState state_ = RunState::Paused;

    std::unordered_map<uint16_t, Breakpoint> breakpoints_;
    std::unordered_set<uint16_t> watchpoints_;
    std::unordered_set<uint16_t> dirty_;

    // When resuming, the breakpoint we are sitting on is skipped for exactly one
    // instruction so execution makes progress instead of re-triggering.
    std::optional<uint16_t> skip_breakpoint_once_;

    // Set by the write hook when a watched address is written during a slice.
    std::optional<uint16_t> watch_hit_;

    // Execution coverage (L1) and self-modifying-code tracking (L2).
    std::array<uint8_t, 65536> coverage_{};   ///< Per-address CoverageFlag bits.
    uint32_t covered_bytes_ = 0;              ///< Count of bytes seen as code.
    std::vector<SmcEvent> smc_events_;        ///< Recorded SMC events (capped).
    uint64_t smc_total_ = 0;                  ///< Total SMC writes detected.
    std::vector<BlockedWrite> blocked_writes_;///< Refused writes to protected memory (capped).
    uint64_t blocked_total_ = 0;              ///< Total refused writes detected.
    uint16_t current_instruction_pc_ = 0;     ///< PC of the instruction now executing.
    bool break_on_smc_ = false;
    bool smc_break_pending_ = false;          ///< Set by the hook to stop a slice.
    static constexpr std::size_t kMaxSmcEvents = 8192;
};

} // namespace z80::dbg

#endif // Z80_DBG_DEBUG_SESSION_H
