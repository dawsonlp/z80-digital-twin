// Copyright (c) 2026 Larry Dawson. Licensed under the MIT License (see LICENSE).
#pragma once
#include "value_analysis.h"

namespace z80::dbg::analysis {
inline constexpr std::string_view kConstructedTacticVersion = "z80-constructed-transfers/1";
struct ConstructedFinding {
    std::string sample_id, status = "not_applicable", pattern, explanation;
    std::optional<std::string> call_sample, pop_sample, push_sample;
    std::optional<ValueNodeId> target_root;
    bool return_role_established = false;
    std::vector<std::string> unresolved;
};
// Exact byte identity through modeled copies, not ancestor membership or numeric
// equality. Aligned findings from the same validated capture are required.
[[nodiscard]] std::vector<ConstructedFinding> AnalyzeConstructedTransfers(
    const TransferCapture& capture, const std::vector<ContinuationFinding>& continuations,
    const ValueAnalysis& values);
json::Value ConstructedFindingJson(const ConstructedFinding& finding);
} // namespace z80::dbg::analysis
