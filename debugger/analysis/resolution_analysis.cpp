// Copyright (c) 2026 Larry Dawson. Licensed under the MIT License (see LICENSE).
#include "resolution_analysis.h"
#include <algorithm>
#include <format>
#include <map>
#include <set>

namespace z80::dbg::analysis {
namespace {
void add_unique(std::vector<std::string>& items, const std::string& item) {
    if (std::find(items.begin(), items.end(), item) == items.end()) items.push_back(item);
}
std::string target_comment(const TransferSample& sample, const TransferFinding& transfer) {
    const auto destination = std::format("${:04X}", sample.event.next_pc);
    if (transfer.taken == false) return "Conditional transfer not taken; next PC " + destination + ".";
    if (transfer.taken == std::nullopt && transfer.mechanism != TransferMechanism::Sequential)
        return "Observed next PC " + destination + "; instruction outcome unresolved or not applicable.";
    switch (transfer.mechanism) {
    case TransferMechanism::Call: return "Observed CALL to " + destination + ".";
    case TransferMechanism::Restart: return "Observed RST to " + destination + ".";
    case TransferMechanism::Return: return "Observed RET to " + destination + ".";
    case TransferMechanism::InterruptReturn: return "Observed interrupt-return instruction to " + destination + ".";
    case TransferMechanism::Jump: return "Observed direct jump to " + destination + ".";
    case TransferMechanism::IndirectJump: return "Observed indirect jump to " + destination + ".";
    case TransferMechanism::Sequential: return "Observed sequential execution to " + destination + ".";
    default: return "Observed next PC " + destination + ".";
    }
}
} // namespace
std::vector<OccurrenceResolution> ResolveTransferFindings(
    const TransferCapture& capture, const std::vector<TransferFinding>& transfers,
    const std::vector<ContinuationFinding>* continuations, const ValueAnalysis* values) {
    // Index graph nodes once; resolution remains bounded by retained evidence.
    std::map<std::string, std::vector<const ValueNode*>> by_sample;
    if (values) for (const auto& node : values->nodes) by_sample[node.sample_id].push_back(&node);
    std::vector<OccurrenceResolution> result;
    for (size_t i = 0; i < capture.samples.size(); ++i) {
        const auto& sample = capture.samples[i];
        const auto& transfer = transfers.at(i);
        const auto* continuation = continuations ? &continuations->at(i) : nullptr;
        const auto* origin = values ? &values->findings.at(i) : nullptr;
        OccurrenceResolution resolved;
        resolved.target_basis = transfer.target_basis;
        resolved.comment = target_comment(sample, transfer);
        const bool value_available = origin && origin->root &&
            (origin->status == ValueStatus::Traced || origin->status == ValueStatus::Partial);
        bool pop_tracked = false;
        if (origin && origin->status != ValueStatus::Unresolved) {
            auto at = by_sample.find(sample.id);
            if (at != by_sample.end()) for (auto* node : at->second)
                pop_tracked |= node->operation == "memory_read" || node->operation == "memory_read_before_trace";
        }
        auto retain = [&](std::string_view tactic, const std::string& reason, std::string_view covered_by = {}) {
            if (covered_by.empty()) add_unique(resolved.unresolved, reason);
            else resolved.resolved_dependencies.push_back({std::string(tactic), reason, std::string(covered_by)});
        };
        for (const auto& reason : transfer.unresolved) {
            std::string_view covered_by;
            if (value_available && (reason == "target register value origin not traced" ||
                reason == "target register not captured; value origin unresolved")) covered_by = kValueTacticVersion;
            if (continuation && reason == "stack effects and continuation relationship not analyzed")
                covered_by = kContinuationTacticVersion;
            retain(kTransferTacticVersion, reason, covered_by);
        }
        if (continuation) {
            for (const auto& reason : continuation->unresolved) {
                const bool covered = pop_tracked && reason == "popped value provenance into registers is not yet tracked";
                retain(kContinuationTacticVersion, reason, covered ? kValueTacticVersion : std::string_view{});
            }
            if (!continuation->explanation.empty()) resolved.comment += " " + continuation->explanation;
        }
        if (origin) for (const auto& reason : origin->unresolved) retain(kValueTacticVersion, reason);
        if (pop_tracked && continuation && std::find(continuation->unresolved.begin(), continuation->unresolved.end(),
                "popped value provenance into registers is not yet tracked") != continuation->unresolved.end())
            resolved.comment += " Popped bytes are tracked into registers by value analysis.";
        if (value_available) {
            resolved.target_basis = origin->status == ValueStatus::Traced ? "traced_value_origin" : "partially_traced_value_origin";
            resolved.comment += origin->status == ValueStatus::Traced ? " Target value ancestry is traced within this capture." :
                " Target value ancestry reaches context predating this trace.";
            // Walk only target ancestors; never infer a role from equal addresses.
            std::vector<ValueNodeId> pending{*origin->root};
            std::set<uint32_t> visited;
            std::set<std::string> calls;
            while (!pending.empty()) {
                const auto id = pending.back(); pending.pop_back();
                if (visited.contains(id.value)) continue;
                if (visited.size() == kMaxResolutionAncestors) {
                    add_unique(resolved.unresolved, "target ancestry summary budget exhausted; full value graph retained");
                    break;
                }
                visited.insert(id.value);
                const auto& node = values->nodes.at(id.value - 1);
                if (node.operation == "call_continuation") calls.insert(node.sample_id);
                pending.insert(pending.end(), node.inputs.begin(), node.inputs.end());
            }
            for (const auto& call : calls) resolved.comment += " Value ancestry includes the continuation created by sample " + call + ".";
        }
        if (continuation && continuation->status == "matched") resolved.target_basis = "matched_call_continuation";
        if (transfer.mechanism == TransferMechanism::IndirectJump ||
            ((transfer.mechanism == TransferMechanism::Return || transfer.mechanism == TransferMechanism::InterruptReturn) &&
             transfer.taken != false && (!continuation || continuation->status != "matched")))
            resolved.comment += " Logical call/return role is not established by these tactics.";
        for (const auto& reason : resolved.unresolved) resolved.comment += " Unresolved: " + reason + ".";
        result.push_back(std::move(resolved));
    }
    return result;
}
json::Value ResolutionJson(const OccurrenceResolution& resolution) {
    using J = json::Value;
    J::Array unresolved, dependencies;
    for (const auto& reason : resolution.unresolved) unresolved.emplace_back(reason);
    for (const auto& item : resolution.resolved_dependencies)
        dependencies.emplace_back(J::Object{{"tactic", item.tactic}, {"reason", item.reason}, {"resolved_by", item.resolved_by}});
    return J::Object{{"resolver", std::string(kResolutionVersion)}, {"comment", resolution.comment},
        {"target_basis", resolution.target_basis}, {"unresolved", std::move(unresolved)},
        {"resolved_dependencies", std::move(dependencies)}, {"scope", "one_observation"}};
}
json::Value SiteResolutionsJson(const TransferCapture& capture, const std::vector<OccurrenceResolution>& resolutions) {
    using J = json::Value;
    // Different executed bytes at an address remain different sites. Equal byte
    // versions can share a presentation; original revisions stay in the capture.
    using Key = std::pair<uint16_t, std::vector<uint8_t>>;
    std::map<Key, std::map<std::string, J::Array>> sites;
    for (size_t i = 0; i < capture.samples.size(); ++i) {
        const auto& sample = capture.samples[i];
        sites[{sample.event.start, sample.event.bytes}][resolutions.at(i).comment].emplace_back(sample.id);
    }
    J::Array result;
    for (const auto& [site, usages] : sites) {
        J::Array bytes, variants;
        for (auto b : site.second) bytes.emplace_back(int(b));
        std::string comment;
        for (const auto& [text, samples] : usages) {
            variants.emplace_back(J::Object{{"comment", text}, {"samples", samples}});
            if (!comment.empty()) comment += "\n";
            comment += text;
        }
        comment += "\nObserved usages only; additional usages remain possible.";
        result.emplace_back(J::Object{{"start", int(site.first)}, {"bytes", std::move(bytes)},
            {"resolver", std::string(kResolutionVersion)}, {"comment", std::move(comment)},
            {"variants", std::move(variants)}, {"destination_sets_closed", false}});
    }
    return result;
}
} // namespace z80::dbg::analysis
