//
// Z80 Digital Twin - ZX Spectrum 48K PAL timing
// Copyright (c) 2025-2026 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//
// The ULA clock tree and the frame/scanline geometry derived from it. Per Chris
// Smith, "The ZX Spectrum ULA": a single 14 MHz master clock divides down —
//   14 MHz master  /2 -> 7 MHz pixel (dot) clock  /2 -> 3.5 MHz CPU clock
// so 1 T-state = 4 master cycles, 1 pixel = 2 master cycles, 2 pixels/T-state.
//
// The master clock is the time-base "ruler", but the CPU is *not* stepped at
// 14 MHz — it counts T-states (the performance invariant). These constants are
// the single source of truth for the derived periods; the ULA subdivides a
// T-state into pixel/master cycles only where a dot-precise effect needs it.
//

#ifndef Z80_MACHINE_SPECTRUM_TIMING_H
#define Z80_MACHINE_SPECTRUM_TIMING_H

#include <cstdint>

namespace z80::machine::spectrum::timing {

// -- Clock tree (Smith) ------------------------------------------------------
inline constexpr uint32_t kMasterHz = 14'000'000;       ///< ULA master clock.
inline constexpr uint32_t kPixelHz  = kMasterHz / 2;     ///< 7 MHz dot clock.
inline constexpr uint32_t kCpuHz    = kMasterHz / 4;     ///< 3.5 MHz CPU clock.

inline constexpr uint32_t kMasterPerT     = 4;  ///< Master cycles per T-state.
inline constexpr uint32_t kMasterPerPixel = 2;  ///< Master cycles per pixel.
inline constexpr uint32_t kPixelsPerT     = 2;  ///< Pixels emitted per T-state.

// -- Frame / scanline geometry (all derived from the ladder) -----------------
inline constexpr uint32_t kTPerLine      = 224;                       ///< T-states per scanline.
inline constexpr uint32_t kLines         = 312;                       ///< Scanlines per frame.
inline constexpr uint32_t kTPerFrame     = kTPerLine * kLines;        ///< 69,888 T/frame.
inline constexpr uint32_t kDisplayStartT = 64 * kTPerLine;            ///< 14,336: first display pixel.

inline constexpr uint32_t kTopBorderLines    = 64;   ///< incl. vertical retrace/sync.
inline constexpr uint32_t kDisplayLines      = 192;
inline constexpr uint32_t kBottomBorderLines = 56;

/// @brief Nominal field rate (≈50.08 Hz on the PAL 48K).
inline constexpr double kFrameRateHz =
    static_cast<double>(kCpuHz) / static_cast<double>(kTPerFrame);

// -- Conversions for the rare sub-T-state (dot-precise) path -----------------
constexpr uint64_t to_master(uint64_t tstates) noexcept { return tstates * kMasterPerT; }
constexpr uint64_t to_pixels(uint64_t tstates) noexcept { return tstates * kPixelsPerT; }

} // namespace z80::machine::spectrum::timing

#endif // Z80_MACHINE_SPECTRUM_TIMING_H
