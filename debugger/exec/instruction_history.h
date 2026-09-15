#ifndef Z80_DBG_INSTRUCTION_HISTORY_H
#define Z80_DBG_INSTRUCTION_HISTORY_H

#include "memory/metadata_memory.h"
#include <algorithm>
#include <cstdint>
#include <deque>
#include <map>
#include <vector>

namespace z80::dbg {

enum class EvidenceState { Unobserved, Observed, Modified, NotRetained, PartialCapture };
enum class ObservationKind { Instruction, MachineTransition };

struct InstructionObservation {
    uint64_t sequence = 0;
    ObservationKind kind = ObservationKind::Instruction;
    uint16_t start = 0, next_pc = 0;
    uint64_t cycles = 0, read_count = 0;
    bool complete_capture = false;
    std::vector<uint8_t> bytes;
    std::vector<uint64_t> revisions;
};

// Immutable bounded observations. Current-memory applicability is computed;
// modifying memory never rewrites the historical record.
class InstructionHistory {
public:
    static constexpr std::size_t kCapacity = 8192;
    InstructionHistory() = default;
    InstructionHistory(const InstructionHistory&) = delete;
    InstructionHistory& operator=(const InstructionHistory&) = delete;
    InstructionHistory(InstructionHistory&&) = delete;
    InstructionHistory& operator=(InstructionHistory&&) = delete;

    void Complete(const MetadataMemory& memory, uint16_t start, uint16_t next_pc, uint64_t cycles) {
        InstructionObservation event;
        event.start = start; event.next_pc = next_pc; event.cycles = cycles;
        event.read_count = memory.InstructionReadCount();
        auto reads = memory.InstructionReads();
        // Operand expressions may read a word's high/low bytes in either C++
        // evaluation order. This is instruction-address order, not bus timing.
        std::sort(reads.begin(), reads.end(), [start](const auto& a, const auto& b) {
            return uint16_t(a.address - start) < uint16_t(b.address - start);
        });
        event.complete_capture = !reads.empty() && reads.size() == event.read_count;
        for (std::size_t i = 0; i < reads.size(); ++i) {
            event.complete_capture &= uint16_t(reads[i].address - start) == i;
            event.bytes.push_back(reads[i].value);
            event.revisions.push_back(reads[i].revision);
        }
        ++counts_[start];
        ++completed_;
        Append(std::move(event));
    }

    void MachineTransition(uint16_t start, uint16_t next_pc, uint64_t cycles) {
        InstructionObservation event;
        event.kind = ObservationKind::MachineTransition;
        event.start = start; event.next_pc = next_pc; event.cycles = cycles;
        Append(std::move(event));
    }

    [[nodiscard]] const InstructionObservation* Latest(uint16_t address) const {
        const auto it = latest_.find(address);
        return it == latest_.end() ? nullptr : it->second;
    }
    [[nodiscard]] EvidenceState State(const InstructionObservation& event, const MetadataMemory& memory) const {
        if (!event.complete_capture) return EvidenceState::PartialCapture;
        for (std::size_t i = 0; i < event.bytes.size(); ++i) {
            const auto address = uint16_t(event.start + i);
            if (memory.Revision(address) != event.revisions[i]) return EvidenceState::Modified;
        }
        return EvidenceState::Observed;
    }
    [[nodiscard]] EvidenceState State(uint16_t address, const MetadataMemory& memory) const {
        if (auto event = Latest(address)) return State(*event, memory);
        return counts_[address] ? EvidenceState::NotRetained : EvidenceState::Unobserved;
    }
    [[nodiscard]] std::vector<uint16_t> RetainedStarts() const {
        std::vector<uint16_t> result;
        for (const auto& [address, event] : latest_) result.push_back(address);
        return result;
    }
    [[nodiscard]] std::vector<uint16_t> Anchors(const MetadataMemory& memory) const {
        std::vector<uint16_t> result;
        for (const auto& [address, event] : latest_)
            if (State(*event, memory) == EvidenceState::Observed) result.push_back(address);
        return result;
    }
    [[nodiscard]] const std::deque<InstructionObservation>& Events() const { return events_; }
    [[nodiscard]] uint64_t Count(uint16_t address) const { return counts_[address]; }
    [[nodiscard]] uint64_t Completed() const { return completed_; }
    [[nodiscard]] uint64_t Dropped() const { return dropped_; }
    [[nodiscard]] uint64_t Generation() const { return generation_; }

    void Clear() {
        events_.clear(); latest_.clear();
        std::fill(counts_.begin(), counts_.end(), 0);
        completed_ = dropped_ = 0;
        ++generation_; // never reuse a UI cache generation or event sequence
    }

private:
    void Append(InstructionObservation event) {
        event.sequence = ++generation_;
        events_.push_back(std::move(event));
        const auto& newest = events_.back();
        if (newest.kind == ObservationKind::Instruction) latest_[newest.start] = &newest;
        if (events_.size() > kCapacity) {
            const auto& oldest = events_.front();
            const auto it = latest_.find(oldest.start);
            if (it != latest_.end() && it->second == &oldest) latest_.erase(it);
            events_.pop_front();
            ++dropped_;
        }
    }

    std::deque<InstructionObservation> events_;
    std::map<uint16_t, const InstructionObservation*> latest_;
    std::vector<uint64_t> counts_ = std::vector<uint64_t>(65536, 0);
    uint64_t completed_ = 0, dropped_ = 0, generation_ = 0;
};

} // namespace z80::dbg
#endif
