// Z80 Digital Twin - optional per-byte metadata memory policy
// Copyright (c) 2026 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
#ifndef Z80_METADATA_MEMORY_H
#define Z80_METADATA_MEMORY_H

#include "observable_memory.h"

namespace z80 {

// A separate CPUImpl memory plug. Composition preserves ObservableMemory's
// observer/protection semantics without adding metadata to that cheaper plug.
// The underlying memory is private so no public write path bypasses revisions.
class MetadataMemory {
public:
    static constexpr std::size_t SIZE = ObservableMemory::SIZE;
    static constexpr std::size_t kCaptureLimit = 256;
    struct InstructionRead { uint16_t address; uint8_t value; uint64_t revision; };

    [[nodiscard]] uint64_t ChangeEpoch() const noexcept { return change_epoch_; }
    [[nodiscard]] uint64_t Revision(uint16_t address) const noexcept { return revisions_[address]; }
    void BeginInstructionCapture() { reads_.clear(); read_count_ = 0; capturing_ = true; }
    void EndInstructionCapture() noexcept { capturing_ = false; }
    [[nodiscard]] const std::vector<InstructionRead>& InstructionReads() const { return reads_; }
    [[nodiscard]] uint64_t InstructionReadCount() const { return read_count_; }
    uint8_t ReadInstructionByte(uint16_t address) {
        const uint8_t value = Read(address);
        if (capturing_) {
            ++read_count_;
            if (reads_.size() < kCaptureLimit) reads_.push_back({address, value, Revision(address)});
        }
        return value;
    }

    using WriteObserver = ObservableMemory::WriteObserver;
    using BlockedWriteObserver = ObservableMemory::BlockedWriteObserver;
    class Reference {
    public:
        Reference(MetadataMemory& owner, uint16_t address) noexcept
            : owner_(owner), address_(address) {}
        operator uint8_t() const noexcept { return owner_.Read(address_); }
        Reference& operator=(uint8_t value) {
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
        Revise(address, value);
        memory_.RawWrite(address, value);
    }
private:
    [[nodiscard]] uint8_t Read(uint16_t address) const noexcept { return memory_[address]; }
    void Revise(uint16_t address, uint8_t value) noexcept {
        if (Read(address) != value) revisions_[address] = ++change_epoch_;
    }
    ObservableMemory memory_;
    std::vector<uint64_t> revisions_ = std::vector<uint64_t>(SIZE, 0);
    uint64_t change_epoch_ = 0;
    bool capturing_ = false;
    uint64_t read_count_ = 0;
    std::vector<InstructionRead> reads_;
};

} // namespace z80
#endif // Z80_METADATA_MEMORY_H
