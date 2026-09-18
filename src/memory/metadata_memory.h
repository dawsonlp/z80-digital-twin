// Z80 Digital Twin - optional per-byte metadata memory policy
// Copyright (c) 2026 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
#ifndef Z80_METADATA_MEMORY_H
#define Z80_METADATA_MEMORY_H

#include "observable_memory.h"
#include <limits>
#include <algorithm>

namespace z80 {

// A separate CPUImpl memory plug. Composition preserves ObservableMemory's
// observer/protection semantics without adding metadata to that cheaper plug.
// The underlying memory is private so no public write path bypasses revisions.
class MetadataMemory {
public:
    static constexpr std::size_t SIZE = ObservableMemory::SIZE;
    using Snapshot = ObservableMemory::Snapshot;
    [[nodiscard]] Snapshot CaptureState() const noexcept { return memory_.CaptureState(); }
    // Revisions stay monotonic: restored bytes invalidate stale observations.
    // The owning debugger starts a new analysis epoch after restoration.
    void RestoreState(const Snapshot& state) noexcept {
        for (uint32_t address = 0; address < SIZE; ++address)
            Revise(static_cast<uint16_t>(address), state.bytes[address]);
        memory_.RestoreState(state);
    }

    static constexpr std::size_t kCaptureLimit = 256;
    struct InstructionRead { uint16_t address; uint8_t value; uint64_t revision; uint64_t changes; };

    enum class AccessKind { Instruction, DataRead, Write, Change, Refused, HostChange, Count };
    struct Stamp { uint64_t sequence = 0, cycles = 0; uint16_t pc = 0; bool interrupt = false; };
    struct Activity { uint64_t count = 0; Stamp first{}, latest{}; uint8_t old_value = 0, new_value = 0; };
    struct ByteMetadata {
        std::array<Activity, static_cast<size_t>(AccessKind::Count)> activity{};
        bool instruction_byte = false, self_modified = false;
    };
    const ByteMetadata& Metadata(uint16_t address) const { return metadata_[address]; }
    void BeginCpuAccess(uint16_t pc, const uint64_t& cycles, bool continuation, bool interrupt) {
        if (!continuation) access_pc_ = pc;
        clock_ = &cycles; cpu_access_ = true; interrupt_access_ = interrupt;
    }
    void EndCpuAccess() { cpu_access_ = false; clock_ = nullptr; }
    bool CpuInstructionAccess() const { return cpu_access_ && !interrupt_access_; }
    void MarkInstructionByte(const InstructionRead& read) {
        auto& info = metadata_[read.address]; info.instruction_byte = true;
        if (info.activity[size_t(AccessKind::Change)].count > read.changes &&
            !info.activity[size_t(AccessKind::Change)].latest.interrupt) info.self_modified = true;
    }
    size_t ActivityStorageBytes() const { return metadata_.capacity() * sizeof(ByteMetadata); }
    void ClearActivity() { std::fill(metadata_.begin(), metadata_.end(), ByteMetadata{}); }
    bool RevisionOverflow() const { return revision_overflow_; }

    [[nodiscard]] uint64_t ChangeEpoch() const noexcept { return change_epoch_; }
    [[nodiscard]] uint64_t Revision(uint16_t address) const noexcept { return revisions_[address]; }
    void BeginInstructionCapture() { reads_.clear(); read_count_ = 0; capturing_ = true; }
    void EndInstructionCapture() noexcept { capturing_ = false; }
    [[nodiscard]] const std::vector<InstructionRead>& InstructionReads() const { return reads_; }
    [[nodiscard]] uint64_t InstructionReadCount() const { return read_count_; }
    uint8_t ReadInstructionByte(uint16_t address) {
        const uint8_t value = Read(address);
        if (cpu_access_) Record(address, AccessKind::Instruction);
        if (capturing_) {
            ++read_count_;
            if (reads_.size() < kCaptureLimit) reads_.push_back({address, value, Revision(address), metadata_[address].activity[size_t(AccessKind::Change)].count});
        }
        return value;
    }

    using WriteObserver = ObservableMemory::WriteObserver;
    using BlockedWriteObserver = ObservableMemory::BlockedWriteObserver;
    class Reference {
    public:
        Reference(MetadataMemory& owner, uint16_t address) noexcept
            : owner_(owner), address_(address) {}
        operator uint8_t() const noexcept {
            if (owner_.cpu_access_) owner_.Record(address_, AccessKind::DataRead);
            return owner_.Read(address_);
        }
        Reference& operator=(uint8_t value) {
            if (owner_.cpu_access_) {
                const auto old = owner_.Read(address_);
                const bool blocked = owner_.WriteProtected(address_);
                owner_.Record(address_, blocked ? AccessKind::Refused : AccessKind::Write, old, value);
                if (!blocked && old != value) {
                    owner_.Record(address_, AccessKind::Change, old, value);
                    if (!owner_.interrupt_access_ && owner_.metadata_[address_].instruction_byte)
                        owner_.metadata_[address_].self_modified = true;
                }
            } else if (!owner_.WriteProtected(address_) && owner_.Read(address_) != value) {
                owner_.Record(address_, AccessKind::HostChange, owner_.Read(address_), value);
            }
            // Revision is visible to observers when the committed write fires.
            if (!owner_.WriteProtected(address_)) owner_.Revise(address_, value);
            owner_.memory_[address_] = value;
            return *this;
        }
        Reference& operator=(const Reference& other) {
            return *this = static_cast<uint8_t>(other);
        }
    private:
        MetadataMemory& owner_;
        uint16_t address_;
    };
    [[nodiscard]] uint8_t operator[](uint16_t address) const noexcept { return Read(address); }
    [[nodiscard]] Reference operator[](uint16_t address) noexcept { return {*this, address}; }

    int AddWriteObserver(WriteObserver observer) { return memory_.AddWriteObserver(std::move(observer)); }
    void RemoveWriteObserver(int id) { memory_.RemoveWriteObserver(id); }
    void ClearWriteObservers() noexcept { memory_.ClearWriteObservers(); }
    [[nodiscard]] bool HasObservers() const noexcept { return memory_.HasObservers(); }
    [[nodiscard]] std::size_t ObserverCount() const noexcept { return memory_.ObserverCount(); }
    int AddBlockedWriteObserver(BlockedWriteObserver observer) {
        return memory_.AddBlockedWriteObserver(std::move(observer));
    }
    void RemoveBlockedWriteObserver(int id) { memory_.RemoveBlockedWriteObserver(id); }
    void SetWriteProtect(uint16_t lo, uint16_t hi) noexcept { memory_.SetWriteProtect(lo, hi); }
    void ClearWriteProtect() noexcept { memory_.ClearWriteProtect(); }
    [[nodiscard]] bool WriteProtected(uint16_t address) const noexcept { return memory_.WriteProtected(address); }
    // Host loads bypass observers/protection, but still invalidate old evidence.
    void RawWrite(uint16_t address, uint8_t value) noexcept {
        if (Read(address) != value) Record(address, AccessKind::HostChange, Read(address), value);
        Revise(address, value);
        memory_.RawWrite(address, value);
    }
private:
    [[nodiscard]] uint8_t Read(uint16_t address) const noexcept { return memory_[address]; }
    void Revise(uint16_t address, uint8_t value) noexcept {
        if (Read(address) != value) {
            if (change_epoch_ == std::numeric_limits<uint64_t>::max()) revision_overflow_ = true;
            else revisions_[address] = ++change_epoch_;
        }
    }
    void Record(uint16_t address, AccessKind kind, uint8_t old = 0, uint8_t value = 0) {
        auto& activity = metadata_[address].activity[static_cast<size_t>(kind)];
        if (sequence_ != std::numeric_limits<uint64_t>::max()) ++sequence_;
        Stamp stamp{sequence_, clock_ ? *clock_ : 0, cpu_access_ ? access_pc_ : uint16_t{0}, cpu_access_ && interrupt_access_};
        if (!activity.count) activity.first = stamp;
        if (activity.count != std::numeric_limits<uint64_t>::max()) ++activity.count;
        activity.latest = stamp; activity.old_value = old; activity.new_value = value;
    }
    std::vector<ByteMetadata> metadata_ = std::vector<ByteMetadata>(SIZE);
    uint64_t sequence_ = 0;
    const uint64_t* clock_ = nullptr;
    uint16_t access_pc_ = 0;
    bool cpu_access_ = false, interrupt_access_ = false, revision_overflow_ = false;
    ObservableMemory memory_;
    std::vector<uint64_t> revisions_ = std::vector<uint64_t>(SIZE, 0);
    uint64_t change_epoch_ = 0;
    bool capturing_ = false;
    uint64_t read_count_ = 0;
    std::vector<InstructionRead> reads_;
};

} // namespace z80
#endif // Z80_METADATA_MEMORY_H
