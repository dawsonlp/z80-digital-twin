//
// Z80 Digital Twin - ObservableMemory policy
// Copyright (c) 2025 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//
// An instrumented 64 KB memory plug for tooling (debugger, machines). It mirrors
// FastMemory's storage but intercepts every write through a proxy reference and
// fans the exact (address, old, new) event out to a list of observers. Multiple
// observers can be attached at once — e.g. the debugger's dirty/SMC/watch
// handler *and* a machine device — so a running machine stays fully debuggable.
//
// Its speed is intentionally irrelevant: tooling drives the CPU at interactive
// rates, and the production/benchmark build never instantiates this type. Reads
// are never hooked, so fetch/operand traffic is untouched.
//

#ifndef Z80_OBSERVABLE_MEMORY_H
#define Z80_OBSERVABLE_MEMORY_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace z80 {

class ObservableMemory {
public:
    static constexpr std::size_t SIZE = 65536;

    /// @brief Called on every committed write, with the byte before and after.
    using WriteObserver = std::function<void(uint16_t address,
                                             uint8_t old_value,
                                             uint8_t new_value)>;

    /// @brief Write-intercepting proxy returned by non-const operator[].
    class Reference {
    public:
        Reference(ObservableMemory& owner, uint16_t address) noexcept
            : owner_(owner), address_(address) {}

        operator uint8_t() const noexcept { return owner_.data_[address_]; }

        Reference& operator=(uint8_t value) {
            const uint8_t old_value = owner_.data_[address_];
            owner_.data_[address_] = value;
            owner_.Notify(address_, old_value, value);
            return *this;
        }

        // Memory-to-memory copy routes through the byte-write path so observers
        // still fire.
        Reference& operator=(const Reference& other) {
            return *this = static_cast<uint8_t>(other);
        }

    private:
        ObservableMemory& owner_;
        uint16_t address_;
    };

    [[nodiscard]] uint8_t operator[](uint16_t address) const noexcept {
        return data_[address];
    }
    [[nodiscard]] Reference operator[](uint16_t address) noexcept {
        return Reference{*this, address};
    }

    // -- Observer registry ---------------------------------------------------

    /// @brief Register a write observer. Returns an id used to remove it later.
    /// @note Observers must not add/remove observers from within their callback
    ///       (the notify loop does not expect the list to mutate mid-write).
    int AddWriteObserver(WriteObserver observer) {
        const int id = next_id_++;
        observers_.emplace_back(id, std::move(observer));
        return id;
    }

    /// @brief Remove a previously-added observer by id (no-op if absent).
    void RemoveWriteObserver(int id) {
        for (auto it = observers_.begin(); it != observers_.end(); ++it) {
            if (it->first == id) { observers_.erase(it); return; }
        }
    }

    void ClearWriteObservers() noexcept { observers_.clear(); }

    [[nodiscard]] bool HasObservers() const noexcept { return !observers_.empty(); }
    [[nodiscard]] std::size_t ObserverCount() const noexcept { return observers_.size(); }

private:
    void Notify(uint16_t address, uint8_t old_value, uint8_t new_value) {
        for (auto& [id, observer] : observers_) {
            if (observer) observer(address, old_value, new_value);
        }
    }

    std::array<uint8_t, SIZE> data_{};
    std::vector<std::pair<int, WriteObserver>> observers_;
    int next_id_ = 0;
};

} // namespace z80

#endif // Z80_OBSERVABLE_MEMORY_H
