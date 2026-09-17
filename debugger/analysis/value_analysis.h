// Copyright (c) 2026 Larry Dawson. Licensed under the MIT License (see LICENSE).
#pragma once
#include "transfer_analysis.h"

namespace z80::dbg::analysis {
struct ValueNodeId {
    uint32_t value = 0; // one-based, scoped to a single analysis result
    auto operator<=>(const ValueNodeId&) const = default;
};
struct ValueNode {
    ValueNodeId id;
    std::string sample_id, operation;
    uint16_t value = 0;
    uint8_t width = 8;
    bool complete = false;
    std::optional<uint16_t> memory_address;
    std::vector<ValueNodeId> inputs;
};
enum class ValueStatus { NotApplicable, Traced, Partial, Unresolved };
struct ValueFinding {
    std::string sample_id, explanation;
    ValueStatus status = ValueStatus::NotApplicable;
    std::optional<ValueNodeId> root;
    std::optional<ValueNodeId> before_sp_root, after_sp_root;
    std::vector<std::string> unresolved;
};
inline constexpr size_t kMaxValueNodes = 32768;
inline constexpr std::string_view kValueTacticVersion = "z80-address-values/3";
struct ValueAnalysis {
    std::vector<ValueNode> nodes;
    std::vector<ValueFinding> findings;
    size_t node_budget = kMaxValueNodes;
    bool exhausted = false;
};
// Input is a validated, ordered transfer capture. Stops across missing context,
// unsupported operations or exhausted budgets; never reads current CPU memory.
[[nodiscard]] ValueAnalysis AnalyzeAddressValues(const TransferCapture& capture, size_t node_budget = kMaxValueNodes);
json::Value ValueFindingJson(const ValueFinding& finding);
json::Value ValueGraphJson(const ValueAnalysis& analysis);
} // namespace z80::dbg::analysis
