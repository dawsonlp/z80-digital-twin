// Copyright (c) 2026 Larry Dawson. Licensed under the MIT License (see LICENSE).
#pragma once
#include "resolution_analysis.h"

namespace z80::dbg::analysis {
inline constexpr std::string_view kTransferGraphVersion = "z80-transfer-graph/1";
// Aligned findings from a validated capture. IDs are report-local, not run IDs.
json::Value TransferGraphJson(const TransferCapture& capture,
    const std::vector<TransferFinding>& transfers,
    const std::vector<OccurrenceResolution>& resolutions);
// Readable projection of TransferGraphJson; does not infer routine boundaries.
std::string TransferGraphText(const json::Value& graph);
}
