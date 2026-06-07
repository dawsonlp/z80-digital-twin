//
// Z80 Digital Twin - OpenBusIo policy
// Copyright (c) 2025-2026 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//
// The honest default I/O device: nothing is attached. `Out` is a transient pulse
// that no peripheral latches, so it is discarded; `In` returns the floating-bus
// value (0xFF on a Spectrum-style bus). There is no implicit per-port storage —
// reading a port does NOT return what was last written. This is what a bare Z80
// with empty I/O space really does, and it is the zero-cost default for the
// performance-reference configuration.
//

#ifndef Z80_OPEN_BUS_IO_H
#define Z80_OPEN_BUS_IO_H

#include <cstdint>

namespace z80 {

class OpenBusIo {
public:
    /// @brief Value a floating data bus reads as (pulled high).
    static constexpr uint8_t kFloating = 0xFF;

    [[nodiscard]] uint8_t In(uint16_t /*port*/) const noexcept { return kFloating; }
    void Out(uint16_t /*port*/, uint8_t /*value*/) const noexcept {}
};

} // namespace z80

#endif // Z80_OPEN_BUS_IO_H
