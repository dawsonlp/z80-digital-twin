// Copyright (c) 2026 Larry Dawson. Licensed under the MIT License.
#pragma once

#include "debug_session.h"
#include <expected>
#include <memory>
#include <string>

namespace z80::dbg {

// In-process only, driven on the execution owner's thread between slices.
// No CPU/memory/device ownership is transferred to this controller.
class LivePatch {
    struct Identity {};
public:
    enum class ErrorCode { Running, IncompleteInstruction, Invalid, Protected,
                           Overlap, Stale, NoCheckpoint, Failed, RestoreFailed };
    struct Error { ErrorCode code; std::string message; };
    template<class T> using Result = std::expected<T, Error>;

    // Machine/front-end adapter captures devices and any presentation state.
    // restore must stage any allocations before touching state. Captures must
    // retain values, not copies of live callback/observer owners.
    struct Devices {
        std::function<bool()> unchanged;
        std::function<void()> restore;
    };
    using CaptureDevices = std::function<Devices()>;
    struct Range { uint32_t address; std::vector<uint8_t> bytes; };

    class Plan {
        friend class LivePatch;
        Plan() = default;
        struct Saved {
            CpuSnapshot cpu;
            MetadataMemory::Snapshot memory;
            std::array<uint64_t, 65536> revisions;
            Devices devices;
            DebugSession::ControlSnapshot controls;
        };
        std::shared_ptr<const Saved> before_;
        std::vector<Range> ranges_;
        CpuSnapshot after_;
        uint64_t generation_ = 0;
        std::shared_ptr<const Identity> owner_;
    public:
        [[nodiscard]] const auto& Ranges() const noexcept { return ranges_; }
        [[nodiscard]] uint8_t BeforeByte(uint16_t address) const noexcept { return before_->memory.bytes[address]; }
        [[nodiscard]] const CpuSnapshot& Before() const noexcept { return before_->cpu; }
        [[nodiscard]] const CpuSnapshot& After() const noexcept { return after_; }
    };

    explicit LivePatch(DebugSession& session, CaptureDevices capture = {})
        : session_(session), capture_devices_(std::move(capture)) {}
    LivePatch(const LivePatch&) = delete;
    LivePatch& operator=(const LivePatch&) = delete;

    [[nodiscard]] Result<Plan> Prepare(std::vector<Range> ranges,
                                      std::optional<CpuSnapshot> state = {});
    [[nodiscard]] Result<void> Apply(const Plan& plan);
    [[nodiscard]] Result<void> SaveCheckpoint();
    [[nodiscard]] Result<void> RestoreCheckpoint();
    [[nodiscard]] bool HasCheckpoint() const noexcept { return bool(checkpoint_); }
    [[nodiscard]] uint64_t Generation() const noexcept { return generation_; }

private:
    [[nodiscard]] Result<void> Ready() const;
    [[nodiscard]] std::shared_ptr<const Plan::Saved> Capture() const;
    void Restore(const Plan::Saved& saved);
    std::shared_ptr<const Identity> identity_ = std::make_shared<const Identity>();
    DebugSession& session_;
    CaptureDevices capture_devices_;
    std::shared_ptr<const Plan::Saved> checkpoint_;
    uint64_t generation_ = 0;
};

} // namespace z80::dbg
