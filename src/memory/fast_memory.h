//
// Z80 Digital Twin - FastMemory policy
// Copyright (c) 2025 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//
// The production memory plug for CPUImpl<Memory>. It is a thin, fully-inlinable
// wrapper over a flat 64 KB array: every access compiles to the same direct
// indexing the emulator used before memory became a pluggable policy, so the
// production/benchmark build carries zero additional overhead.
//

#ifndef Z80_FAST_MEMORY_H
#define Z80_FAST_MEMORY_H

#include <array>
#include <cstddef>
#include <cstdint>

namespace z80 {

/// @brief Zero-overhead 64 KB memory plug (production / benchmark default).
///
/// Satisfies the CPU's @c Memory policy contract: read and write a byte by
/// 16-bit address via @c operator[]. Both accessors are @c noexcept and trivial
/// to inline. Addresses wrap naturally to 16 bits, matching the Z80 address
/// space.
class FastMemory {
public:
    static constexpr std::size_t SIZE = 65536;

    /// @brief Read a byte (const access).
    [[nodiscard]] uint8_t operator[](uint16_t address) const noexcept {
        return data_[address];
    }

    /// @brief Read/write a byte; returns a reference for direct assignment.
    [[nodiscard]] uint8_t& operator[](uint16_t address) noexcept {
        return data_[address];
    }

private:
    // Value-initialized so a freshly constructed CPU sees a deterministic,
    // zeroed address space (RAII: the object owns a fully-defined state on
    // construction). Correct programs are unaffected; this only removes the
    // previous reliance on indeterminate startup memory.
    std::array<uint8_t, SIZE> data_{};
};

} // namespace z80

#endif // Z80_FAST_MEMORY_H
