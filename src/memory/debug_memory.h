//
// Z80 Digital Twin - DebugMemory policy
// Copyright (c) 2025 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//
// The debugger memory plug for CPUImpl<Memory>. It mirrors FastMemory's storage
// but intercepts every write through a proxy reference, delivering exact
// (address, old, new) write events to an installed hook. This is what powers
// memory change-highlighting and write-watchpoints in the debugger. Its speed
// is intentionally irrelevant: the debugger drives the CPU at interactive rates,
// and the production build never instantiates this type.
//

#ifndef Z80_DEBUG_MEMORY_H
#define Z80_DEBUG_MEMORY_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>

namespace z80 {

/// @brief Instrumented 64 KB memory plug used by the debugger.
///
/// Satisfies the CPU's @c Memory policy contract. Reads return a plain byte;
/// writes go through @ref Reference, which fires the optional write hook after
/// committing the store. With no hook installed, behaviour is identical to
/// FastMemory (the branch is a single null check).
class DebugMemory {
public:
    static constexpr std::size_t SIZE = 65536;

    /// @brief Callback invoked on every committed write.
    /// @param address  Address written (16-bit).
    /// @param old_value Byte present before the write.
    /// @param new_value Byte written.
    using WriteHook = std::function<void(uint16_t address,
                                         uint8_t old_value,
                                         uint8_t new_value)>;

    /// @brief Write-intercepting proxy returned by non-const operator[].
    ///
    /// Converts to @c uint8_t for reads and forwards assignments to the owning
    /// DebugMemory so the hook fires. Holds only a reference and an address, so
    /// it is cheap to create and never outlives the expression that produced it.
    class Reference {
    public:
        Reference(DebugMemory& owner, uint16_t address) noexcept
            : owner_(owner), address_(address) {}

        /// @brief Read-through conversion.
        operator uint8_t() const noexcept { return owner_.data_[address_]; }

        /// @brief Write a byte; commits the store then notifies the hook.
        Reference& operator=(uint8_t value) {
            const uint8_t old_value = owner_.data_[address_];
            owner_.data_[address_] = value;
            if (owner_.write_hook_) {
                owner_.write_hook_(address_, old_value, value);
            }
            return *this;
        }

        /// @brief Memory-to-memory copy (e.g. block transfers) routes through
        ///        the byte-write path so the hook still fires.
        Reference& operator=(const Reference& other) {
            return *this = static_cast<uint8_t>(other);
        }

    private:
        DebugMemory& owner_;
        uint16_t address_;
    };

    /// @brief Read a byte (const access).
    [[nodiscard]] uint8_t operator[](uint16_t address) const noexcept {
        return data_[address];
    }

    /// @brief Obtain a write-intercepting proxy for the given address.
    [[nodiscard]] Reference operator[](uint16_t address) noexcept {
        return Reference{*this, address};
    }

    /// @brief Install (or replace) the write hook. Pass {} to clear.
    void SetWriteHook(WriteHook hook) { write_hook_ = std::move(hook); }

    /// @brief Remove any installed write hook.
    void ClearWriteHook() noexcept { write_hook_ = nullptr; }

    /// @brief Whether a write hook is currently installed.
    [[nodiscard]] bool HasWriteHook() const noexcept {
        return static_cast<bool>(write_hook_);
    }

private:
    std::array<uint8_t, SIZE> data_{};
    WriteHook write_hook_{};
};

} // namespace z80

#endif // Z80_DEBUG_MEMORY_H
