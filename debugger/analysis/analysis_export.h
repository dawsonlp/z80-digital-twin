// Copyright (c) 2026 Larry Dawson. Licensed under the MIT License (see LICENSE).
#pragma once
#include "analysis_storage.h"
namespace z80::dbg::analysis {
struct ExportOptions {
    uint32_t offset = 0;
    std::optional<uint32_t> length;
    bool aliases = false;
    std::vector<SymbolId> selected; // Empty selects all active records; ambiguity is an error.
};
struct ExportArtifacts {
    std::string assembly, source_map, manifest;
};
[[nodiscard]] Result<ExportArtifacts> ExportPasmo(const Project &project, std::span<const uint8_t> image,
                                                  const ExportOptions &options = {});
// A deliberately conservative Pasmo 0.5.5 identifier subset, case-fold unique.
bool PasmoIdentifier(std::string_view name);
} // namespace z80::dbg::analysis
