// Copyright (c) 2026 Larry Dawson. Licensed under the MIT License (see LICENSE).
#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
namespace z80::dbg {
std::string Sha256(std::span<const uint8_t> bytes);
inline std::string Sha256(std::string_view text) {
    return Sha256(std::span(reinterpret_cast<const uint8_t *>(text.data()), text.size()));
}
} // namespace z80::dbg
