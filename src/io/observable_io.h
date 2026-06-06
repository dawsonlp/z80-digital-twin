//
// Z80 Digital Twin - ObservableIo decorator
// Copyright (c) 2025 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//
// Wraps another I/O device and records every transaction the CPU performs — the
// debugger's faithful view of the bus. It forwards In/Out to the inner device
// unchanged (so device semantics and any side effects are preserved exactly),
// and logs (port, value, direction, sequence) for a passive transaction view.
//
// Crucially this records only the I/O the *program* actually does; a UI reads
// the log, it must never call In() itself (a real IN can have side effects).
//

#ifndef Z80_OBSERVABLE_IO_H
#define Z80_OBSERVABLE_IO_H

#include <cstddef>
#include <cstdint>
#include <vector>

namespace z80 {

template <class Inner>
class ObservableIo {
public:
    struct Transaction {
        uint16_t port = 0;
        uint8_t value = 0;
        bool is_out = false;   ///< true = OUT (write), false = IN (read)
        uint64_t seq = 0;      ///< monotonic order of the transaction
    };

    [[nodiscard]] uint8_t In(uint16_t port) {
        const uint8_t value = inner_.In(port);
        record(port, value, /*is_out=*/false);
        return value;
    }
    void Out(uint16_t port, uint8_t value) {
        inner_.Out(port, value);
        record(port, value, /*is_out=*/true);
    }

    // -- Inner device + transaction log (for tooling) ------------------------
    [[nodiscard]] Inner& inner() noexcept { return inner_; }
    [[nodiscard]] const Inner& inner() const noexcept { return inner_; }

    [[nodiscard]] const std::vector<Transaction>& Transactions() const noexcept { return log_; }
    [[nodiscard]] uint64_t TransactionCount() const noexcept { return total_; }
    void ClearTransactions() noexcept { log_.clear(); }

private:
    void record(uint16_t port, uint8_t value, bool is_out) {
        ++total_;
        if (log_.size() < kMaxLog) log_.push_back({port, value, is_out, total_});
    }

    Inner inner_;
    std::vector<Transaction> log_;
    uint64_t total_ = 0;
    static constexpr std::size_t kMaxLog = 4096;
};

} // namespace z80

#endif // Z80_OBSERVABLE_IO_H
