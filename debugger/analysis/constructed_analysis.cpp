// Copyright (c) 2026 Larry Dawson. Licensed under the MIT License (see LICENSE).
#include "constructed_analysis.h"
#include <array>
#include <format>
#include <map>

namespace z80::dbg::analysis {
namespace {
struct ByteIdentity {
    std::optional<size_t> call, pop, push;
    unsigned lane = 0;
};
using Identity = std::array<ByteIdentity, 2>;
uint8_t opcode(const TransferSample& sample) {
    for (auto byte : sample.event.bytes) if (byte != 0xDD && byte != 0xFD) return byte;
    return 0;
}
bool is_pop(const TransferSample& sample) { return (opcode(sample) & 0xCF) == 0xC1; }
bool is_push(const TransferSample& sample) { return (opcode(sample) & 0xCF) == 0xC5; }
bool continuous(const TransferSample& previous, const TransferSample& sample) {
    return previous.stack && sample.stack && previous.stack->complete_data_accesses &&
        sample.stack->complete_data_accesses && sample.stack->previous == previous.id &&
        sample.event.start == previous.event.next_pc && sample.stack->before_sp == previous.stack->after_sp;
}
struct ActiveCall { size_t index; bool stack_bytes_intact = true; };
} // namespace
std::vector<ConstructedFinding> AnalyzeConstructedTransfers(
    const TransferCapture& capture, const std::vector<ContinuationFinding>& continuations,
    const ValueAnalysis& values) {
    std::map<std::string, size_t> indices;
    for (size_t i = 0; i < capture.samples.size(); ++i) indices.emplace(capture.samples[i].id, i);
    // Forward transfer over the bounded DAG: O(nodes), with no recursive walk.
    std::vector<Identity> identities(values.nodes.size());
    for (const auto& node : values.nodes) {
        const auto sample_index = indices.at(node.sample_id);
        const auto& sample = capture.samples[sample_index];
        Identity identity;
        auto input = [&](size_t i) -> const Identity& { return identities.at(node.inputs.at(i).value - 1); };
        if (node.operation == "call_continuation") {
            identity[0].call = identity[1].call = sample_index;
            identity[1].lane = 1;
        } else if (node.operation == "join_le") {
            identity[0] = input(0)[0]; identity[1] = input(1)[0];
        } else if (node.operation == "high_byte") identity[0] = input(0)[1];
        else if (node.operation == "low_byte") identity[0] = input(0)[0];
        else if (node.operation == "register_copy" || node.operation == "exchange") identity = input(0);
        else if (node.operation == "memory_read") {
            // Input 0 is the read byte's previous version; subsequent inputs are
            // address dependencies and must never confer continuation identity.
            identity[0] = input(0)[0];
            if (is_pop(sample)) identity[0].pop = sample_index;
        } else if (node.operation == "memory_write") {
            identity[0] = input(0)[0];
            identity[0].push = is_push(sample) ? std::optional<size_t>(sample_index) : std::nullopt;
        }
        // Arithmetic, unknown entry values and pre-trace memory intentionally
        // carry no identity, even if they produce a coincident numeric address.
        identities.at(node.id.value - 1) = identity;
    }
    std::vector<ActiveCall> active;
    std::vector<ConstructedFinding> result;
    for (size_t i = 0; i < capture.samples.size(); ++i) {
        const auto& sample = capture.samples[i];
        const auto& value = values.findings.at(i);
        const auto& continuation = continuations.at(i);
        const auto transfer = ClassifyTransfer(sample);
        ConstructedFinding finding; finding.sample_id = sample.id;
        if (!i || !continuous(capture.samples[i - 1], sample) || value.status == ValueStatus::Unresolved ||
            transfer.mechanism == TransferMechanism::InterruptReturn) active.clear();
        // Preserve exact stack-slot lifetime for the helper primitive. A POP or
        // committed overwrite consumes/invalidates a saved slot, even at equal value.
        if (sample.stack) for (auto& call : active) {
            const auto slot = capture.samples[call.index].stack->after_sp;
            for (const auto& access : sample.stack->accesses)
                if ((access.kind == DataAccessKind::Write ||
                    (access.kind == DataAccessKind::Read && is_pop(sample))) &&
                    (access.address == slot || access.address == uint16_t(slot + 1))) call.stack_bytes_intact = false;
        }
        const bool jump = transfer.mechanism == TransferMechanism::IndirectJump;
        const bool ret = transfer.mechanism == TransferMechanism::Return && transfer.taken == true;
        if ((jump || ret) && value.root && value.status != ValueStatus::Unresolved) {
            finding.target_root = value.root;
            const auto& identity = identities.at(value.root->value - 1);
            const auto& low = identity[0]; const auto& high = identity[1];
            if (jump && low.call && low.call == high.call && low.lane == 0 && high.lane == 1 &&
                low.pop && low.pop == high.pop && !active.empty() && active.back().index == *low.call) {
                const auto& call = capture.samples[*low.call];
                const auto& pop = capture.samples[*low.pop];
                if (pop.stack->before_sp == call.stack->after_sp && pop.stack->after_sp == call.stack->before_sp &&
                    sample.stack->before_sp == call.stack->before_sp) {
                    finding.status = "matched"; finding.pattern = "popped_continuation_jump";
                    finding.call_sample = call.id; finding.pop_sample = pop.id;
                    finding.return_role_established = true;
                    finding.explanation = std::format("Observed register-mediated return to ${:04X}: continuation from sample {} was consumed by {} and carried unchanged into this jump.", sample.event.next_pc, call.id, pop.id);
                    active.pop_back(); // A saved continuation cannot return twice.
                }
            }
            if (ret && low.push && low.push == high.push) {
                const auto& push = capture.samples[*low.push];
                if (push.stack->after_sp == sample.stack->before_sp && push.stack->before_sp == sample.stack->after_sp) {
                    finding.status = "matched"; finding.pattern = "pushed_target_ret"; finding.push_sample = push.id;
                    // Even without promoting PUSH/RET to a logical return, do
                    // not allow the same call token to be consumed a second time.
                    if (low.call && low.call == high.call && low.lane == 0 && high.lane == 1) {
                        if (!active.empty() && active.back().index == *low.call) active.pop_back();
                        else active.clear();
                    }
                    finding.explanation = std::format("Observed stack-mediated transfer to ${:04X}: RET consumed the target bytes written by PUSH sample {}. This does not by itself establish a logical return.", sample.event.next_pc, push.id);
                }
            }
            if (jump && finding.status != "matched" && !active.empty() && active.back().stack_bytes_intact) {
                const auto& call = capture.samples[active.back().index];
                if (sample.stack->before_sp == call.stack->after_sp) {
                    finding.status = "matched"; finding.pattern = "continuation_preserving_indirect_jump";
                    finding.call_sample = call.id;
                    finding.explanation = std::format("Observed indirect transfer to ${:04X} with the saved continuation from sample ", sample.event.next_pc) + call.id +
                        " still intact at the current SP. A call helper and an internal jump remain possible interpretations.";
                }
            }
            if (finding.status != "matched" && continuation.status != "matched") {
                finding.status = "unresolved";
                finding.unresolved.push_back("no supported constructed-transfer relationship for this observed target");
            }
        } else if (jump || ret) {
            finding.status = "unresolved";
            finding.unresolved.push_back("target value evidence unavailable for constructed-transfer analysis");
        }
        if (jump && finding.status != "matched" && !active.empty() && !active.back().stack_bytes_intact)
            active.clear(); // Unclassified exit after slot consumption may have returned.
        if (transfer.mechanism == TransferMechanism::Jump && transfer.taken == true && !active.empty()) {
            const auto& call = capture.samples[active.back().index];
            if (!active.back().stack_bytes_intact && sample.event.next_pc == uint16_t(call.event.start + call.event.bytes.size()))
                active.clear(); // A direct exit may have consumed this invocation.
        }
        if (continuation.status == "matched") {
            if (!active.empty() && capture.samples[active.back().index].id == continuation.call_sample) active.pop_back();
            else active.clear();
        } else if (ret && finding.pattern != "pushed_target_ret") active.clear();
        if (continuation.status == "created" && value.status != ValueStatus::Unresolved) active.push_back({i, true});
        result.push_back(std::move(finding));
    }
    return result;
}
json::Value ConstructedFindingJson(const ConstructedFinding& finding) {
    using J = json::Value;
    auto optional = [](const std::optional<std::string>& value) { return value ? J(*value) : J{}; };
    J::Array unresolved;
    for (const auto& reason : finding.unresolved) unresolved.emplace_back(reason);
    return J::Object{{"sample_id", finding.sample_id}, {"tactic", std::string(kConstructedTacticVersion)},
        {"status", finding.status}, {"pattern", finding.pattern}, {"explanation", finding.explanation},
        {"call_sample", optional(finding.call_sample)}, {"pop_sample", optional(finding.pop_sample)},
        {"push_sample", optional(finding.push_sample)}, {"target_root", finding.target_root ? J(finding.target_root->value) : J{}},
        {"return_role_established", finding.return_role_established}, {"unresolved", std::move(unresolved)}};
}
} // namespace z80::dbg::analysis
