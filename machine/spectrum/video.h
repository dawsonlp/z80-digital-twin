//
// Z80 Digital Twin - ZX Spectrum frame rendering (border + display)
// Copyright (c) 2025-2026 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//
// Assembles a whole frame of palette indices: the border around the 256x192
// display, with each display line decoded via screen.h. The Spectrum's
// interleaved display-memory layout collapses here into a per-line base-address
// calculation (no separate deinterleave pass).
//
// Everything the renderer needs is read through a FrameSource — "what did the
// beam see here?" — so render fidelity is a property of the source, not the
// renderer. The simple final-memory source reads current RAM (correct for
// static screens, the boot screen, and rainbow borders driven by a per-line
// border timeline). A future beam-accurate source can resolve bytes as of each
// line's fetch time without changing this code.
//

#ifndef Z80_MACHINE_SPECTRUM_VIDEO_H
#define Z80_MACHINE_SPECTRUM_VIDEO_H

#include "screen.h"

#include <algorithm>
#include <array>
#include <concepts>
#include <cstdint>
#include <span>

namespace z80::machine::spectrum::video {

// -- Frame layout: the 256x192 display plus a (tunable) border ---------------
inline constexpr int kDisplayWidth  = 256;
inline constexpr int kDisplayHeight = 192;
inline constexpr int kBorderLeft   = 32;
inline constexpr int kBorderRight  = 32;
inline constexpr int kBorderTop    = 32;
inline constexpr int kBorderBottom = 32;
inline constexpr int kFrameWidth   = kBorderLeft + kDisplayWidth + kBorderRight;    // 320
inline constexpr int kFrameHeight  = kBorderTop + kDisplayHeight + kBorderBottom;   // 256
inline constexpr int kFramePixels  = kFrameWidth * kFrameHeight;                    // 81,920

// -- Display-memory address mapping (the interleaved Spectrum layout) --------

/// @brief Address of the bitmap byte for display line (0..191), column (0..31).
///        The 32 bytes of a line are contiguous (column is the low 5 bits).
[[nodiscard]] constexpr uint16_t bitmap_address(int display_line, int column = 0) noexcept {
    const int y = display_line;
    return static_cast<uint16_t>(
        0x4000 + (((y & 0xC0) << 5) | ((y & 0x38) << 2) | ((y & 0x07) << 8)) + column);
}

/// @brief Address of the attribute byte for display line (0..191), column (0..31).
[[nodiscard]] constexpr uint16_t attribute_address(int display_line, int column = 0) noexcept {
    return static_cast<uint16_t>(0x5800 + ((display_line >> 3) << 5) + column);
}

// -- The render seam ---------------------------------------------------------

/// @brief Answers "what did the beam see here?" for one frame. The display_line
///        argument lets a beam-accurate source resolve a byte as of that line's
///        fetch time; the final-memory source ignores it.
template <class T>
concept FrameSource = requires(const T src, int line, uint16_t addr) {
    { src.border_for_line(line) } -> std::convertible_to<uint8_t>;
    { src.screen_byte(addr, line) } -> std::convertible_to<uint8_t>;
};

/// @brief Whether a rendered row falls in the display band; sets @p display_line.
[[nodiscard]] constexpr bool display_row(int rendered_line, int& display_line) noexcept {
    if (rendered_line < kBorderTop || rendered_line >= kBorderTop + kDisplayHeight)
        return false;
    display_line = rendered_line - kBorderTop;
    return true;
}

// -- Rendering ---------------------------------------------------------------

/// @brief Render one rendered scanline (kFrameWidth palette indices) into @p row.
template <FrameSource Src>
void render_scanline(const Src& src, int rendered_line, bool flash_on,
                     std::span<uint8_t> row) {
    const uint8_t border = src.border_for_line(rendered_line);

    int y = 0;
    if (!display_row(rendered_line, y)) {
        std::fill(row.begin(), row.end(), border);   // a border-only line
        return;
    }

    std::fill_n(row.begin(), kBorderLeft, border);

    std::array<uint8_t, 32> bitmap{};
    std::array<uint8_t, 32> attributes{};
    for (int x = 0; x < 32; ++x) {
        bitmap[x]     = src.screen_byte(bitmap_address(y, x), y);
        attributes[x] = src.screen_byte(attribute_address(y, x), y);
    }
    const std::array<uint8_t, 256> pixels = screen::decode_line(bitmap, attributes, flash_on);
    std::copy(pixels.begin(), pixels.end(), row.begin() + kBorderLeft);

    std::fill_n(row.begin() + (kBorderLeft + kDisplayWidth), kBorderRight, border);
}

/// @brief Render a whole frame (kFramePixels palette indices, row-major) into @p out.
template <FrameSource Src>
void render_frame(const Src& src, bool flash_on, std::span<uint8_t> out) {
    for (int r = 0; r < kFrameHeight; ++r) {
        render_scanline(src, r, flash_on,
                        out.subspan(static_cast<std::size_t>(r) * kFrameWidth, kFrameWidth));
    }
}

} // namespace z80::machine::spectrum::video

#endif // Z80_MACHINE_SPECTRUM_VIDEO_H
