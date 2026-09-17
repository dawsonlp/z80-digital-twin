// Copyright (c) 2026 Larry Dawson. Licensed under the MIT License (see LICENSE).
#pragma once
#include "stack_analysis.h"

namespace z80::dbg::analysis {
inline constexpr std::string_view kResolutionVersion = "z80-evidence-resolution/3";
inline constexpr size_t kMaxResolutionAncestors = 4096;
struct ResolvedDependency {
    std::string tactic, reason, resolved_by;
};
struct OccurrenceResolution {
    std::string target_basis, comment, continuation_relationship = "not_run";
    std::vector<std::string> unresolved;
    std::vector<ResolvedDependency> resolved_dependencies;
};
// Resolve aligned, validated findings without changing them. Null pointers mean
// a tactic has not run, not that it ran and found nothing. No CPU execution.
[[nodiscard]] std::vector<OccurrenceResolution> ResolveTransferFindings(
    const TransferCapture& capture, const std::vector<TransferFinding>& transfers,
    const std::vector<ContinuationFinding>* continuations = nullptr,
    const ValueAnalysis* values = nullptr,
    const std::vector<ConstructedFinding>* constructed = nullptr,
    const std::vector<StackFinding>* stack = nullptr);
json::Value ResolutionJson(const OccurrenceResolution& resolution);
json::Value SiteResolutionsJson(const TransferCapture& capture,
                               const std::vector<OccurrenceResolution>& resolutions);
} // namespace z80::dbg::analysis
