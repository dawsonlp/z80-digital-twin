// Copyright (c) 2026 Larry Dawson. Licensed under the MIT License (see LICENSE).
#pragma once
#include "constructed_analysis.h"
namespace z80::dbg::analysis {
inline constexpr std::string_view kStackTacticVersion = "z80-stack-reconstruction/1";
struct StackFinding {
    std::string sample_id, status = "not_applicable", pattern, explanation;
    std::optional<std::string> call_sample;
    std::vector<std::string> supporting_samples, skipped_calls, unresolved;
    std::optional<ValueNodeId> target_root, sp_root;
    bool return_role_established = false;
};
// Reads aligned findings for one validated capture; never changes CPU state.
[[nodiscard]] std::vector<StackFinding> AnalyzeStackReconstruction(
    const TransferCapture& capture, const std::vector<ContinuationFinding>& continuations,
    const ValueAnalysis& values, const std::vector<ConstructedFinding>& constructed);
json::Value StackFindingJson(const StackFinding& finding);
} // namespace z80::dbg::analysis
