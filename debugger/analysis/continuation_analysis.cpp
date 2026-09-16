// Copyright (c) 2026 Larry Dawson. Licensed under the MIT License (see LICENSE).
#include "transfer_analysis.h"
#include <format>
#include <map>

namespace z80::dbg::analysis {
namespace {
struct SavedByte {
    std::string call;
    uint16_t continuation;
    uint8_t value;
    bool high;
};
bool access_pair(const StackEvidence& stack, DataAccessKind kind, uint16_t low, uint16_t value) {
    // Exact two-byte stack effect. Ordering is deliberately not claimed to be
    // bus order (the existing CPU may evaluate word-byte reads in either order).
    if (stack.accesses.size() != 2) return false;
    unsigned low_count = 0, high_count = 0;
    for (const auto& access : stack.accesses) {
        if (access.kind != kind) return false;
        if (access.address == low && access.value == uint8_t(value)) ++low_count;
        else if (access.address == uint16_t(low + 1) && access.value == uint8_t(value >> 8)) ++high_count;
        else return false;
    }
    return low_count == 1 && high_count == 1;
}
uint8_t base_opcode(const InstructionObservation& event) {
    for (auto byte : event.bytes) if (byte != 0xDD && byte != 0xFD) return byte;
    return 0;
}
bool access_addresses(const StackEvidence& stack, DataAccessKind kind, uint16_t low) {
    if (stack.accesses.size() != 2) return false;
    const auto& a = stack.accesses[0]; const auto& b = stack.accesses[1];
    return a.kind == kind && b.kind == kind &&
        ((a.address == low && b.address == uint16_t(low + 1)) ||
         (b.address == low && a.address == uint16_t(low + 1)));
}
} // namespace

std::vector<ContinuationFinding> AnalyzeContinuations(const TransferCapture& capture) {
    std::map<uint16_t, SavedByte> saved;
    std::vector<ContinuationFinding> findings;
    const TransferSample* previous = nullptr;
    for (const auto& sample : capture.samples) {
        ContinuationFinding finding;
        finding.sample_id = sample.id;
        const auto transfer = ClassifyTransfer(sample);
        const bool call = transfer.mechanism == TransferMechanism::Call || transfer.mechanism == TransferMechanism::Restart;
        const bool ret = transfer.mechanism == TransferMechanism::Return;
        auto gap = [&](std::string reason) { saved.clear(); finding.unresolved.push_back(std::move(reason)); };
        const auto* stack = sample.stack ? &*sample.stack : nullptr;
        bool effect_unresolved = false;
        for (const auto& reason : transfer.unresolved)
            effect_unresolved |= reason != "stack effects and continuation relationship not analyzed" &&
                reason != "target register value origin not traced" &&
                reason != "target register not captured; value origin unresolved";
        if (!stack || !stack->complete_data_accesses || !sample.event.complete_capture || effect_unresolved ||
            transfer.mechanism == TransferMechanism::MachineTransition || transfer.mechanism == TransferMechanism::Unknown) {
            gap("complete, consistent instruction and data-access evidence required");
            findings.push_back(std::move(finding)); previous = &sample; continue;
        }
        if (!previous || !stack->previous || *stack->previous != previous->id || !previous->stack ||
            !previous->stack->complete_data_accesses || previous->stack->after_sp != stack->before_sp ||
            previous->event.next_pc != sample.event.start)
            gap("continuity to preceding sample not established; older stack lineage discarded");

        // Contradictory reads invalidate the producer's asserted uninterrupted
        // history. Mixed reads/writes are handled conservatively; this rung does
        // not model value flow within an instruction or claim bus ordering.
        for (const auto& access : stack->accesses) {
            const auto known = saved.find(access.address);
            if (access.kind == DataAccessKind::Read && known != saved.end() && known->second.value != access.value)
                gap("read contradicts retained stack-byte lineage");
        }

        if (call && transfer.taken == true) {
            const auto continuation = uint16_t(sample.event.start + sample.event.bytes.size());
            const auto low = uint16_t(stack->before_sp - 2);
            // Writes invalidate provenance even if the numeric value is unchanged.
            for (const auto& access : stack->accesses)
                if (access.kind == DataAccessKind::Write) saved.erase(access.address);
            if (stack->after_sp == low && access_pair(*stack, DataAccessKind::Write, low, continuation)) {
                saved.insert_or_assign(low, SavedByte{sample.id, continuation, uint8_t(continuation), false});
                saved.insert_or_assign(uint16_t(low + 1), SavedByte{sample.id, continuation, uint8_t(continuation >> 8), true});
                finding.status = "created"; finding.call_sample = sample.id;
                finding.explanation = std::format("This invocation saved continuation ${:04X} at stack ${:04X}.", continuation, low);
            } else gap("observed call stack writes do not establish a saved continuation");
        } else if (ret && transfer.taken == true) {
            const auto low = stack->before_sp;
            const auto high = uint16_t(low + 1);
            const auto l = saved.find(low), h = saved.find(high);
            if (stack->after_sp == uint16_t(low + 2) &&
                access_pair(*stack, DataAccessKind::Read, low, sample.event.next_pc) &&
                l != saved.end() && h != saved.end() && l->second.call == h->second.call &&
                !l->second.high && h->second.high && l->second.continuation == sample.event.next_pc &&
                h->second.continuation == sample.event.next_pc &&
                l->second.value == uint8_t(sample.event.next_pc) && h->second.value == uint8_t(sample.event.next_pc >> 8)) {
                finding.status = "matched"; finding.call_sample = l->second.call;
                finding.explanation = std::format("This return consumed continuation ${:04X} saved by sample {}.",
                                                  sample.event.next_pc, l->second.call);
                // One saved continuation cannot be consumed twice merely because
                // the RAM bytes remain after a pop.
                saved.erase(low); saved.erase(high);
            } else gap("no unbroken lineage from a captured call to these consumed stack bytes");
        } else {
            for (const auto& access : stack->accesses)
                if (access.kind == DataAccessKind::Write) saved.erase(access.address);
            const auto opcode = base_opcode(sample.event);
            if ((call || ret) && transfer.taken == false && !stack->accesses.empty()) {
                gap("untaken call/return has unexplained data accesses");
            } else if ((opcode & 0xCF) == 0xC1 && stack->after_sp == uint16_t(stack->before_sp + 2) &&
                       access_addresses(*stack, DataAccessKind::Read, stack->before_sp)) {
                // POP consumes a slot but this rung does not track its value into registers.
                saved.erase(stack->before_sp); saved.erase(uint16_t(stack->before_sp + 1));
                finding.status = "not_applicable";
                finding.unresolved.push_back("popped value provenance into registers is not yet tracked");
            } else if ((opcode & 0xCF) == 0xC5 && stack->after_sp == uint16_t(stack->before_sp - 2) &&
                       access_addresses(*stack, DataAccessKind::Write, stack->after_sp)) {
                finding.status = "not_applicable"; // PUSH does not establish a call.
            } else if (stack->before_sp != stack->after_sp) {
                gap("unmodeled stack-pointer transition; older continuation lineage discarded");
            } else finding.status = "not_applicable";
        }
        findings.push_back(std::move(finding));
        previous = &sample;
    }
    return findings;
}
} // namespace z80::dbg::analysis
