//
// Z80 Digital Twin - Device interface
// Copyright (c) 2025 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//
// A Device is a peripheral with a per-frame lifecycle hook. Devices interact
// with the CPU primarily through the compile-time I/O policy (ports) and the
// memory plug (writes); this interface adds only the timing-driven work that
// belongs to the machine's frame clock — FLASH-attribute toggling, audio-buffer
// flushing, etc. The frame interrupt itself is asserted by the Machine, which
// owns the frame clock.
//

#ifndef Z80_MACHINE_DEVICE_H
#define Z80_MACHINE_DEVICE_H

namespace z80::machine {

class Device {
public:
    virtual ~Device() = default;

    /// @brief Called once per emulated frame, after the frame has run.
    virtual void OnFrame() {}
};

} // namespace z80::machine

#endif // Z80_MACHINE_DEVICE_H
