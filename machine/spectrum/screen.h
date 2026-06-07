//
// Z80 Digital Twin - ZX Spectrum screen decoding
// Copyright (c) 2025-2026 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//
// Decodes the Spectrum's display bytes into palette indices. Ported to modern
// C++23 from the author's C decoder (projects/spectrum/spectrum_screen.c): the
// attribute model, the per-byte (8-pixel) decoder, and the per-line (32-byte ->
// 256-pixel) decoder, plus the 16-entry palette.
//
// What's here is the colour/bit logic for one display line. The screen-memory
// deinterleave (the thirds x char-row x pixel-row address order, 6144 bitmap +
// 768 attribute bytes -> 192 linear lines) belongs with the ULA and lands next.
//
// FLASH: an attribute's bit 7 marks the cell as flashing. The ULA swaps that
// cell's ink and paper every 16 frames; callers pass the current phase (see
// flash_phase()) so the decode reflects what the real screen would show.
//

#ifndef Z80_MACHINE_SPECTRUM_SCREEN_H
#define Z80_MACHINE_SPECTRUM_SCREEN_H

#include <array>
#include <cstdint>
#include <span>

namespace z80::machine::spectrum::screen {

// -- Attribute byte layout ---------------------------------------------------
inline constexpr uint8_t kFlashMask  = 0x80;  ///< bit 7: FLASH (swap ink/paper).
inline constexpr uint8_t kBrightMask = 0x40;  ///< bit 6: BRIGHT (palette 8..15).
inline constexpr uint8_t kPaperMask  = 0x38;  ///< bits 5..3: paper colour 0..7.
inline constexpr uint8_t kInkMask    = 0x07;  ///< bits 2..0: ink colour 0..7.

/// @brief A decoded attribute byte: ink/paper as 0..15 palette indices (BRIGHT
///        folded in) plus the FLASH flag.
struct Attribute {
    uint8_t ink = 0;
    uint8_t paper = 0;
    bool flash = false;
};

/// @brief Decode one attribute byte. BRIGHT (bit 6) becomes palette bit 3, so
///        ink/paper are ready-to-use indices into kPalette.
[[nodiscard]] constexpr Attribute decode_attribute(uint8_t bits) noexcept {
    const uint8_t bright = (bits & kBrightMask) ? 0x08u : 0x00u;
    return Attribute{
        .ink   = static_cast<uint8_t>((bits & kInkMask) | bright),
        .paper = static_cast<uint8_t>(((bits & kPaperMask) >> 3) | bright),
        .flash = (bits & kFlashMask) != 0,
    };
}

// -- FLASH phase --------------------------------------------------------------
inline constexpr uint64_t kFlashHalfFrames   = 16;                      ///< Frames per FLASH half-cycle.
inline constexpr uint64_t kFlashPeriodFrames = 2 * kFlashHalfFrames;    ///< 32-frame FLASH cycle.

/// @brief Whether the FLASH phase is in its "swapped" half for @p frame_counter
///        (the ULA toggles flashing cells every 16 frames).
[[nodiscard]] constexpr bool flash_phase(uint64_t frame_counter) noexcept {
    return (frame_counter % kFlashPeriodFrames) >= kFlashHalfFrames;
}

// -- Pixel decoding -----------------------------------------------------------

/// @brief Expand one 8-pixel bitmap byte into 8 palette indices. The leftmost
///        pixel is the most-significant bit. A set bit takes ink, a clear bit
///        paper; if the cell flashes and @p flash_on is set, the two swap.
[[nodiscard]] constexpr std::array<uint8_t, 8>
decode_byte(uint8_t bitmap, uint8_t attribute, bool flash_on) noexcept {
    const Attribute attr = decode_attribute(attribute);
    const bool swap = flash_on && attr.flash;
    const uint8_t ink   = swap ? attr.paper : attr.ink;
    const uint8_t paper = swap ? attr.ink   : attr.paper;

    std::array<uint8_t, 8> out{};
    for (std::size_t bit = 0; bit < 8; ++bit) {
        const bool set = (bitmap >> (7 - bit)) & 1u;
        out[bit] = set ? ink : paper;
    }
    return out;
}

/// @brief Decode one 256-pixel display line: 32 bitmap bytes against 32
///        per-character attribute bytes, into 256 palette indices.
[[nodiscard]] constexpr std::array<uint8_t, 256>
decode_line(std::span<const uint8_t, 32> bitmap,
            std::span<const uint8_t, 32> attributes,
            bool flash_on) noexcept {
    std::array<uint8_t, 256> out{};
    for (std::size_t col = 0; col < 32; ++col) {
        const std::array<uint8_t, 8> cell =
            decode_byte(bitmap[col], attributes[col], flash_on);
        for (std::size_t p = 0; p < 8; ++p)
            out[col * 8 + p] = cell[p];
    }
    return out;
}

// -- Palette ------------------------------------------------------------------
inline constexpr uint8_t kDim    = 200;  ///< Non-BRIGHT colour level.
inline constexpr uint8_t kBright = 255;  ///< BRIGHT colour level.

/// @brief A palette colour. Spectrum colour bits are blue=1, red=2, green=4.
struct Rgb {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;

    friend constexpr bool operator==(const Rgb&, const Rgb&) = default;
};

namespace detail {
constexpr std::array<Rgb, 16> make_palette() noexcept {
    std::array<Rgb, 16> palette{};
    for (std::size_t i = 0; i < 16; ++i) {
        const uint8_t level = (i & 0x08) ? kBright : kDim;
        palette[i] = Rgb{
            static_cast<uint8_t>((i & 0x02) ? level : 0),   // bit 1 -> red
            static_cast<uint8_t>((i & 0x04) ? level : 0),   // bit 2 -> green
            static_cast<uint8_t>((i & 0x01) ? level : 0),   // bit 0 -> blue
        };
    }
    return palette;
}
} // namespace detail

/// @brief The 16-colour palette: indices 0..7 normal, 8..15 BRIGHT.
inline constexpr std::array<Rgb, 16> kPalette = detail::make_palette();

/// @brief Map a 0..15 palette index to its colour.
[[nodiscard]] constexpr Rgb to_rgb(uint8_t palette_index) noexcept {
    return kPalette[palette_index & 0x0F];
}

} // namespace z80::machine::spectrum::screen

#endif // Z80_MACHINE_SPECTRUM_SCREEN_H
