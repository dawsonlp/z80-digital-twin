// Copyright (c) 2026 Larry Dawson. Licensed under the MIT License (see LICENSE).
#pragma once
#include "analysis_json.h"
#include "analysis_project.h"
#include <filesystem>

namespace z80::dbg::analysis {
json::Value ProjectJson(const Project &project); // Canonical semantic payload.
std::string Revision(const Project &project);
std::string Serialize(const Project &project);
[[nodiscard]] Result<Project> Deserialize(std::string_view text, const Image &expected_image);
[[nodiscard]] Result<std::string> ReadText(const std::filesystem::path &path);
struct Loaded {
    Project project;
    std::string disk_token;
};
[[nodiscard]] Result<Loaded> Open(const std::filesystem::path &path, const Image &expected_image);
// expected_token absent means create only. Existing paths require the token
// returned by Open/Save. Uses a cooperating writer lock, sibling rename and .bak.
[[nodiscard]] Result<std::string> Save(const Project &project, const std::filesystem::path &path,
                                       const std::optional<std::string> &expected_token = {});
} // namespace z80::dbg::analysis
