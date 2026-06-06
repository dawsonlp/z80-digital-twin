//
// Z80 Digital Twin - CallbackIo policy
// Copyright (c) 2025 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//
// A generic I/O bridge: IN/OUT are forwarded to user-installed handlers. This
// keeps machine-specific port decoding (e.g. the ZX Spectrum ULA, which lives
// in the machine layer) out of the CPU core, while still letting an OUT pulse
// reach a device and an IN sample live device state. With no handler installed
// it behaves exactly like an open bus (OUT discarded, IN -> 0xFF), so it stays
// honest about the no-storage I/O model.
//

#ifndef Z80_CALLBACK_IO_H
#define Z80_CALLBACK_IO_H

#include <cstdint>
#include <functional>
#include <utility>

namespace z80 {

class CallbackIo {
public:
    using OutHandler = std::function<void(uint16_t port, uint8_t value)>;
    using InHandler  = std::function<uint8_t(uint16_t port)>;

    /// @brief Value a floating data bus reads as when no handler is installed.
    static constexpr uint8_t kFloating = 0xFF;

    void Out(uint16_t port, uint8_t value) { if (out_) out_(port, value); }
    [[nodiscard]] uint8_t In(uint16_t port) { return in_ ? in_(port) : kFloating; }

    /// @brief Install the OUT / IN handlers (typically wired to a device).
    void OnOut(OutHandler handler) { out_ = std::move(handler); }
    void OnIn(InHandler handler) { in_ = std::move(handler); }

private:
    OutHandler out_;
    InHandler in_;
};

} // namespace z80

#endif // Z80_CALLBACK_IO_H
