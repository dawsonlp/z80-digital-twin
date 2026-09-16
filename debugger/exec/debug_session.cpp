//
// Z80 Digital Twin Debugger - DebugSession implementation
// Copyright (c) 2025-2026 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//

#include "debug_session.h"

#include <algorithm>

namespace z80::dbg {

DebugSession::DebugSession(DebugCPU& cpu) : cpu_(cpu) {
    reader_ = [this](uint16_t address) { return cpu_.ReadMemory(address); };
    write_observer_id_ = cpu_.GetMemory().AddWriteObserver(
        [this](uint16_t address, uint8_t old_value, uint8_t new_value) {
            OnMemoryWrite(address, old_value, new_value);
        });
    blocked_observer_id_ = cpu_.GetMemory().AddBlockedWriteObserver(
        [this](uint16_t address, uint8_t current_value, uint8_t attempted_value) {
            OnBlockedWrite(address, current_value, attempted_value);
        });
    state_ = cpu_.IsHalted() ? RunState::Halted : RunState::Paused;
}

DebugSession::~DebugSession() {
    cpu_.GetMemory().EndInstructionCapture();
    cpu_.GetMemory().RemoveWriteObserver(write_observer_id_);
    cpu_.GetMemory().RemoveBlockedWriteObserver(blocked_observer_id_);
}

void DebugSession::OnBlockedWrite(uint16_t address, uint8_t current_value,
                                  uint8_t attempted_value) {
    // A write to write-protected memory (ROM) was refused: the byte is unchanged.
    // Record it as its own category — it resembles SMC but is semantically
    // different (read-only memory, not self-modifying code).
    coverage_[address] |= kBlockedWrite;
    ++blocked_total_;
    if (blocked_writes_.size() < kMaxSmcEvents) {
        blocked_writes_.push_back({address, current_value, attempted_value,
                                   current_instruction_pc_, cpu_.GetCycleCount()});
    }
}

void DebugSession::OnMemoryWrite(uint16_t address, uint8_t old_value,
                                 uint8_t new_value) {
    dirty_.insert(address);
    if (watchpoints_.find(address) != watchpoints_.end()) {
        watch_hit_ = address;
    }

    if (old_value == new_value) return; // a write is not necessarily a change
    if (cpu_.GetMemory().CpuInstructionAccess()) {
        history_.CodeWrite(address);
        for (const auto& read : cpu_.GetMemory().InstructionReads())
            if (read.address == address) history_.MarkSelfModified(current_instruction_pc_);
    }

    // Self-modifying code: a write to a byte that has executed as code (L2).
    if (cpu_.GetMemory().CpuInstructionAccess() &&
        (coverage_[address] & (kExecOpcode | kExecOperand))) {
        coverage_[address] |= kSelfModified;
        ++smc_total_;
        if (smc_events_.size() < kMaxSmcEvents) {
            smc_events_.push_back({address, old_value, new_value,
                                   current_instruction_pc_, cpu_.GetCycleCount()});
        }
        if (break_on_smc_) smc_break_pending_ = true;
    }
}

void DebugSession::RecordCoverage(uint16_t start, uint32_t decoded_length) {
    if (coverage_[start] & kExecOpcode) return;   // this start is already mapped
    auto mark = [&](uint16_t a, uint8_t flag) {
        if ((coverage_[a] & (kExecOpcode | kExecOperand)) == 0) ++covered_bytes_;
        coverage_[a] |= flag;
    };
    mark(start, kExecOpcode);
    for (uint32_t i = 1; i < decoded_length; ++i)
        mark(static_cast<uint16_t>(start + i), kExecOperand);
}

void DebugSession::PrepareExecution() {
    if (prepare_execution_) {
        const auto pc = cpu_.PC();
        const auto cycles = cpu_.GetCycleCount();
        prepare_execution_();
        if (pc != cpu_.PC() || cycles != cpu_.GetCycleCount())
            history_.MachineTransition(pc, cpu_.PC(), cpu_.GetCycleCount() - cycles);
    }
}

bool DebugSession::ExecuteOneInstruction(uint32_t stage_budget) {
    if (!instruction_pending_) {
        current_instruction_pc_ = cpu_.PC();
        pending_decoded_length_ = disasm_.Decode(reader_, current_instruction_pc_).length;
        instruction_pending_ = true;
        instruction_start_cycle_ = cpu_.GetCycleCount();
        cpu_.GetMemory().BeginInstructionCapture();
    }
    const auto result = cpu_.StepInstruction(std::min(stage_budget, uint32_t{4096}));
    if (result.completed) {
        cpu_.GetMemory().EndInstructionCapture();
        history_.Complete(cpu_.GetMemory(), current_instruction_pc_, cpu_.PC(),
                          cpu_.GetCycleCount() - instruction_start_cycle_);
        for (const auto& read : cpu_.GetMemory().InstructionReads())
            cpu_.GetMemory().MarkInstructionByte(read);
        RecordCoverage(current_instruction_pc_, pending_decoded_length_);
        instruction_pending_ = false;
    }
    if (advance_execution_) advance_execution_();
    return result.completed;
}

bool DebugSession::BreakpointStopsAt(uint16_t pc) const {
    const auto it = breakpoints_.find(pc);
    return it != breakpoints_.end() && it->second.enabled;
}

StepResult DebugSession::StepInstruction(uint32_t stage_budget) {
    if (stage_budget == 0) return {StopReason::IncompleteInstruction, 0, cpu_.PC()};
    const uint64_t before = cpu_.GetCycleCount();
    PrepareExecution();
    if (cpu_.IsHalted()) {
        state_ = RunState::Halted;
        return {StopReason::AlreadyHalted, 0, cpu_.PC()};
    }

    watch_hit_.reset();
    skip_breakpoint_once_.reset();
    smc_break_pending_ = false;

    const bool completed = ExecuteOneInstruction(stage_budget);
    const uint64_t cycles = cpu_.GetCycleCount() - before;

    StopReason reason;
    if (!completed) {
        reason = StopReason::IncompleteInstruction;
    } else if (watch_hit_) {
        reason = StopReason::Watchpoint;
    } else if (smc_break_pending_) {
        reason = StopReason::SelfModified;
    } else if (cpu_.IsHalted()) {
        reason = StopReason::Halted;
    } else {
        reason = StopReason::StepComplete;
    }
    state_ = cpu_.IsHalted() ? RunState::Halted : RunState::Paused;
    return {reason, cycles, cpu_.PC()};
}

StepResult DebugSession::StepOver() {
    if (instruction_pending_) return StepInstruction();
    const uint64_t before = cpu_.GetCycleCount();
    PrepareExecution();
    if (cpu_.IsHalted()) {
        state_ = RunState::Halted;
        return {StopReason::AlreadyHalted, 0, cpu_.PC()};
    }

    const uint16_t pc = cpu_.PC();
    const Instruction ins = disasm_.Decode(reader_, pc);
    const bool subroutine = (ins.mnemonic == "CALL" || ins.mnemonic == "RST");
    if (!subroutine) {
        auto result = StepInstruction();
        result.cycles = cpu_.GetCycleCount() - before;
        return result;
    }

    // Set a (temporary) breakpoint at the return address and run until reached.
    const uint16_t ret = static_cast<uint16_t>(pc + ins.length);
    const bool preexisting = HasBreakpoint(ret);
    if (!preexisting) {
        AddBreakpoint(ret, /*temporary=*/true);
    }

    Run();
    StepResult last{StopReason::BudgetExhausted, 0, pc};
    // Bound the synchronous run so a non-returning subroutine cannot hang.
    constexpr int kMaxSlices = 100000;
    for (int i = 0; i < kMaxSlices && state_ == RunState::Running; ++i) {
        last = RunSlice(1 << 16);
    }

    if (!preexisting) {
        RemoveBreakpoint(ret);   // no-op if it already auto-removed on hit
    }

    // Reaching the return address is a completed step-over, not a "breakpoint".
    StopReason reason = last.reason;
    if (cpu_.PC() == ret && reason == StopReason::Breakpoint) {
        reason = StopReason::StepComplete;
    }
    return {reason, cpu_.GetCycleCount() - before, cpu_.PC()};
}

StepResult DebugSession::RunSlice(uint64_t max_instructions) {
    if (state_ != RunState::Running) {
        state_ = RunState::Running;
    }
    watch_hit_.reset();
    smc_break_pending_ = false;

    const uint64_t before = cpu_.GetCycleCount();
    StopReason reason = StopReason::BudgetExhausted;

    for (uint64_t i = 0; i < max_instructions; ++i) {
        PrepareExecution();
        if (cpu_.IsHalted()) {
            state_ = RunState::Halted;
            reason = StopReason::Halted;
            break;
        }
        const uint16_t pc = cpu_.PC();

        if (!instruction_pending_ && BreakpointStopsAt(pc)) {
            const bool resuming_here =
                skip_breakpoint_once_ && *skip_breakpoint_once_ == pc;
            if (!resuming_here) {
                Breakpoint& bp = breakpoints_[pc];
                ++bp.hit_count;
                const bool temporary = bp.temporary;
                if (temporary) {
                    breakpoints_.erase(pc);
                }
                // Remember this stop so the next resume steps past it once.
                skip_breakpoint_once_ = pc;
                state_ = RunState::Paused;
                reason = StopReason::Breakpoint;
                break;
            }
        }
        // The skip applies to at most the first instruction of the slice.
        skip_breakpoint_once_.reset();

        if (!ExecuteOneInstruction()) {
            state_ = RunState::Paused;
            reason = StopReason::IncompleteInstruction;
            break;
        }

        if (watch_hit_) {
            state_ = RunState::Paused;
            reason = StopReason::Watchpoint;
            break;
        }
        if (smc_break_pending_) {
            state_ = RunState::Paused;
            reason = StopReason::SelfModified;
            break;
        }
        if (cpu_.IsHalted()) {
            state_ = RunState::Halted;
            reason = StopReason::Halted;
            break;
        }
    }

    return {reason, cpu_.GetCycleCount() - before, cpu_.PC()};
}

StepResult DebugSession::RunForTStates(uint64_t tstate_budget) {
    if (state_ != RunState::Running) {
        state_ = RunState::Running;
    }
    watch_hit_.reset();
    smc_break_pending_ = false;

    const uint64_t before = cpu_.GetCycleCount();
    StopReason reason = StopReason::BudgetExhausted;

    // Mirrors RunSlice's per-instruction body, but bounds by elapsed T-states
    // rather than an instruction count (the natural unit for a frame quantum).
    while (cpu_.GetCycleCount() - before < tstate_budget) {
        PrepareExecution();
        if (cpu_.IsHalted()) {
            state_ = RunState::Halted;
            reason = StopReason::Halted;
            break;
        }
        const uint16_t pc = cpu_.PC();

        if (!instruction_pending_ && BreakpointStopsAt(pc)) {
            const bool resuming_here =
                skip_breakpoint_once_ && *skip_breakpoint_once_ == pc;
            if (!resuming_here) {
                Breakpoint& bp = breakpoints_[pc];
                ++bp.hit_count;
                if (bp.temporary) {
                    breakpoints_.erase(pc);
                }
                skip_breakpoint_once_ = pc;
                state_ = RunState::Paused;
                reason = StopReason::Breakpoint;
                break;
            }
        }
        skip_breakpoint_once_.reset();

        if (!ExecuteOneInstruction()) {
            state_ = RunState::Paused;
            reason = StopReason::IncompleteInstruction;
            break;
        }

        if (watch_hit_) {
            state_ = RunState::Paused;
            reason = StopReason::Watchpoint;
            break;
        }
        if (smc_break_pending_) {
            state_ = RunState::Paused;
            reason = StopReason::SelfModified;
            break;
        }
        if (cpu_.IsHalted()) {
            state_ = RunState::Halted;
            reason = StopReason::Halted;
            break;
        }
    }

    return {reason, cpu_.GetCycleCount() - before, cpu_.PC()};
}

void DebugSession::ResetCpu() {
    cpu_.Reset();
    state_ = RunState::Paused;
    dirty_.clear();
    watch_hit_.reset();
    skip_breakpoint_once_.reset();
    instruction_pending_ = false;
    cpu_.GetMemory().EndInstructionCapture();
    smc_break_pending_ = false;
}

bool DebugSession::ClearAnalysis() {
    if (instruction_pending_) return false;
    cpu_.GetMemory().EndInstructionCapture();
    cpu_.GetMemory().ClearActivity();
    history_.Clear();
    coverage_.fill(0);
    covered_bytes_ = 0;
    smc_events_.clear();
    smc_total_ = 0;
    blocked_writes_.clear();
    blocked_total_ = 0;
    smc_break_pending_ = false;
    return true;
}

void DebugSession::Reset() {
    ResetCpu();
    ClearAnalysis();
}

void DebugSession::AddBreakpoint(uint16_t address, bool temporary) {
    auto [it, inserted] = breakpoints_.try_emplace(address);
    Breakpoint& bp = it->second;
    bp.address = address;
    bp.enabled = true;            // adding (re-)enables the breakpoint
    if (inserted) {
        bp.temporary = temporary; // the temporary flag is fixed at creation
        bp.hit_count = 0;
    }
}

void DebugSession::RemoveBreakpoint(uint16_t address) {
    breakpoints_.erase(address);
    if (skip_breakpoint_once_ && *skip_breakpoint_once_ == address) {
        skip_breakpoint_once_.reset();
    }
}

void DebugSession::ToggleBreakpoint(uint16_t address) {
    const auto it = breakpoints_.find(address);
    if (it == breakpoints_.end()) {
        AddBreakpoint(address);
    } else {
        it->second.enabled = !it->second.enabled;
    }
}

bool DebugSession::HasBreakpoint(uint16_t address) const {
    return breakpoints_.find(address) != breakpoints_.end();
}

std::vector<Breakpoint> DebugSession::Breakpoints() const {
    std::vector<Breakpoint> out;
    out.reserve(breakpoints_.size());
    for (const auto& [addr, bp] : breakpoints_) {
        out.push_back(bp);
    }
    std::sort(out.begin(), out.end(),
              [](const Breakpoint& a, const Breakpoint& b) {
                  return a.address < b.address;
              });
    return out;
}

std::vector<uint16_t> DebugSession::Watchpoints() const {
    std::vector<uint16_t> out(watchpoints_.begin(), watchpoints_.end());
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace z80::dbg
