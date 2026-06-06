//
// Z80 Digital Twin - LatchedIo policy
// Copyright (c) 2025 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//
// A bank of 256 read/write latches keyed by the low port byte — i.e. the old
// "ports are an array" behaviour, named for what it actually is: ONE simplistic
// device, not how I/O works in general. It is a legitimate model for a parallel
// output latch and is handy for tests and simple IoT peripherals where a port
// that remembers its last write is exactly what you want. The high address byte
// is ignored (these latches decode only A0–A7).
//

#ifndef Z80_LATCHED_IO_H
#define Z80_LATCHED_IO_H

#include <array>
#include <cstddef>
#include <cstdint>

namespace z80 {

class LatchedIo {
public:
    static constexpr std::size_t PORTS = 256;

    [[nodiscard]] uint8_t In(uint16_t port) const noexcept { return data_[port & 0xFF]; }
    void Out(uint16_t port, uint8_t value) noexcept { data_[port & 0xFF] = value; }

    // Explicit, side-effect-free access for tests/tooling.
    [[nodiscard]] uint8_t Peek(uint8_t port) const noexcept { return data_[port]; }
    void Poke(uint8_t port, uint8_t value) noexcept { data_[port] = value; }

private:
    std::array<uint8_t, PORTS> data_{};
};

} // namespace z80

#endif // Z80_LATCHED_IO_H
