// Copyright (c) 2026 Larry Dawson. Licensed under the MIT License.
#include "live_patch.h"
#include <bitset>
#include <stdexcept>

namespace z80::dbg {
namespace {
std::string failure_message() {
    try { throw; }
    catch (const std::exception& e) { return e.what(); }
    catch (...) { return "Unknown host callback failure"; }
}
auto fail(LivePatch::ErrorCode code, std::string message) {
    return std::unexpected(LivePatch::Error{code, std::move(message)});
}
}

LivePatch::Result<void> LivePatch::Ready() const {
    if (session_.State() == RunState::Running)
        return fail(ErrorCode::Running, "Pause before saving, reviewing or applying state");
    if (!session_.AtInstructionBoundary())
        return fail(ErrorCode::IncompleteInstruction, "Complete the pending instruction first");
    return {};
}

std::shared_ptr<const LivePatch::Plan::Saved> LivePatch::Capture() const {
    auto saved = std::make_shared<Plan::Saved>();
    saved->cpu = *session_.Cpu().CaptureState();
    saved->memory = session_.Cpu().GetMemory().CaptureState();
    for (uint32_t address = 0; address < 65536; ++address)
        saved->revisions[address] = session_.Cpu().GetMemory().Revision(uint16_t(address));
    saved->controls = session_.CaptureControls();
    if (capture_devices_) saved->devices = capture_devices_();
    return saved;
}

LivePatch::Result<LivePatch::Plan> LivePatch::Prepare(std::vector<Range> ranges,
                                                    std::optional<CpuSnapshot> state) {
    if (auto ready = Ready(); !ready) return std::unexpected(ready.error());
    if (session_.RecoveryRequired()) return fail(ErrorCode::RestoreFailed, "Restore checkpoint before reviewing another patch");
    if (session_.Cpu().GetMemory().RevisionOverflow())
        return fail(ErrorCode::Stale, "Memory revision counters exhausted; start a fresh session");
    std::bitset<65536> destinations;
    for (const auto& range : ranges) {
        if (range.bytes.empty() || range.address >= 65536 || range.bytes.size() > 65536 - range.address)
            return fail(ErrorCode::Invalid, "Patch range is empty or outside the 64-KB address space");
        for (uint32_t offset = 0; offset < range.bytes.size(); ++offset) {
            const auto address = uint16_t(range.address + offset);
            if (session_.Cpu().GetMemory().WriteProtected(address))
                return fail(ErrorCode::Protected, "Patch would overwrite protected memory");
            if (destinations.test(address))
                return fail(ErrorCode::Overlap, "Patch ranges overlap; choose one value per address");
            destinations.set(address);
        }
    }
    try {
        Plan plan;
        plan.before_ = Capture();
        plan.after_ = state.value_or(plan.before_->cpu);
        if (plan.after_.interrupt_mode > 2 || plan.after_.cycles != plan.before_->cpu.cycles)
            return fail(ErrorCode::Invalid, "State edit has invalid interrupt mode or changes machine time");
        plan.ranges_ = std::move(ranges);
        plan.generation_ = generation_;
        plan.owner_ = identity_;
        return plan;
    } catch (...) {
        return fail(ErrorCode::Failed, failure_message());
    }
}

void LivePatch::Restore(const Plan::Saved& saved) {
    // Device restore allocates/stages before entry; do it before no-throw CPU
    // and memory restoration. Callbacks/observers retain their original owners.
    auto controls = saved.controls; // stage allocations before any mutation
    if (saved.devices.restore) saved.devices.restore();
    session_.Cpu().GetMemory().RestoreState(saved.memory);
    if (!session_.Cpu().RestoreState(saved.cpu)) throw std::runtime_error("CPU restoration rejected");
    session_.RestoreControls(std::move(controls));
    session_.Cpu().GetIo().ClearTransactions();
    if (session_.Cpu().CaptureState() != saved.cpu ||
        session_.Cpu().GetMemory().CaptureState() != saved.memory ||
        (saved.devices.unchanged && !saved.devices.unchanged()))
        throw std::runtime_error("Checkpoint restoration readback mismatch");
    session_.AfterHostMutation(true);
}

LivePatch::Result<void> LivePatch::Apply(const Plan& plan) {
    if (auto ready = Ready(); !ready) return ready;
    if (session_.RecoveryRequired()) return fail(ErrorCode::RestoreFailed, "Restore checkpoint before applying another patch");
    if (plan.owner_ != identity_ || !plan.before_ || plan.generation_ != generation_)
        return fail(ErrorCode::Stale, "Review belongs to a different or changed session; review again");
    try {
        const auto& before = *plan.before_;
        if (session_.Cpu().GetMemory().RevisionOverflow())
            return fail(ErrorCode::Stale, "Memory revision counters exhausted; start a fresh session");
        if (session_.Cpu().CaptureState() != before.cpu ||
            session_.Cpu().GetMemory().CaptureState() != before.memory)
            return fail(ErrorCode::Stale, "CPU or memory changed since review; review again");
        for (uint32_t address = 0; address < 65536; ++address)
            if (session_.Cpu().GetMemory().Revision(uint16_t(address)) != before.revisions[address])
                return fail(ErrorCode::Stale, "Memory was modified since review; review again");
        if (before.devices.unchanged && !before.devices.unchanged())
            return fail(ErrorCode::Stale, "Device state changed since review; review again");
    } catch (...) {
        return fail(ErrorCode::Failed, failure_message());
    }

    // Keep recovery even if application or rollback fails. Every byte is a host
    // write through normal observers, so raster state is updated at frozen time.
    checkpoint_ = plan.before_;
    try {
        auto expected_memory = plan.before_->memory;
        for (const auto& range : plan.ranges_)
            for (uint32_t offset = 0; offset < range.bytes.size(); ++offset)
                expected_memory.bytes[range.address + offset] = range.bytes[offset];
        for (const auto& range : plan.ranges_)
            for (uint32_t offset = 0; offset < range.bytes.size(); ++offset)
                session_.Cpu().WriteMemory(uint16_t(range.address + offset), range.bytes[offset]);
        if (!session_.Cpu().RestoreState(plan.after_)) throw std::runtime_error("CPU state edit rejected");
        if (session_.Cpu().GetMemory().CaptureState() != expected_memory)
            throw std::runtime_error("Patch readback mismatch (including retained memory/protection)");
        if (session_.Cpu().CaptureState() != plan.after_) throw std::runtime_error("State readback mismatch");
        session_.AfterHostMutation(false);
        ++generation_;
        return {};
    } catch (...) {
        const std::string reason = failure_message();
        ++generation_;
        try { Restore(*checkpoint_); }
        catch (...) {
            session_.RequireRecovery();
            return fail(ErrorCode::RestoreFailed, "Apply failed: " + reason +
                        "; restoration failed: " + failure_message() + "; remain stopped");
        }
        return fail(ErrorCode::Failed, "Apply failed and checkpoint restored: " + reason);
    }
}

LivePatch::Result<void> LivePatch::SaveCheckpoint() {
    if (auto ready = Ready(); !ready) return ready;
    if (session_.RecoveryRequired()) return fail(ErrorCode::RestoreFailed, "Keep recovery checkpoint until restoration succeeds");
    try { checkpoint_ = Capture(); return {}; }
    catch (...) { return fail(ErrorCode::Failed, failure_message()); }
}

LivePatch::Result<void> LivePatch::RestoreCheckpoint() {
    if (auto ready = Ready(); !ready) return ready;
    if (!checkpoint_) return fail(ErrorCode::NoCheckpoint, "No in-process checkpoint saved");
    try {
        Restore(*checkpoint_);
        ++generation_;
        return {};
    } catch (...) {
        session_.RequireRecovery();
        return fail(ErrorCode::RestoreFailed, failure_message());
    }
}
} // namespace z80::dbg
