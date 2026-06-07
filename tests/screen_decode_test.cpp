//
// Z80 Digital Twin - ZX Spectrum screen-decode verification
// Copyright (c) 2025-2026 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//
// Verifies the colour/bit logic ported from the C decoder: attribute decode,
// per-byte and per-line pixel expansion, the FLASH ink/paper swap, the palette,
// and the FLASH phase. A few checks are static_asserts to prove the decoders
// are usable at compile time.
//

#include "spectrum/screen.h"

#include <array>
#include <cstdint>
#include <iostream>

namespace {

namespace s = z80::machine::spectrum::screen;

int failures = 0;
void check(bool ok, const char* what) {
    std::cout << (ok ? "  ✓ " : "  ✗ ") << what << '\n';
    if (!ok) ++failures;
}

// -- Compile-time checks (decoders are constexpr) ----------------------------

// Attribute: ink 0..7 + paper 0..7, BRIGHT folds into bit 3, FLASH = bit 7.
static_assert(s::decode_attribute(0x07).ink == 7);
static_assert(s::decode_attribute(0x07).paper == 0);
static_assert(s::decode_attribute(0x38).paper == 7);          // bits 5..3
static_assert(s::decode_attribute(0x40 | 0x07).ink == 15);    // BRIGHT ink
static_assert(s::decode_attribute(0x40 | 0x38).paper == 15);  // BRIGHT paper
static_assert(s::decode_attribute(0x80).flash == true);
static_assert(s::decode_attribute(0x07).flash == false);

// Byte: MSB is the leftmost pixel; set -> ink (7), clear -> paper (0).
static_assert(s::decode_byte(0x80, 0x07, false)[0] == 7);
static_assert(s::decode_byte(0x80, 0x07, false)[1] == 0);
static_assert(s::decode_byte(0x01, 0x07, false)[7] == 7);

// FLASH swap only when the cell flashes AND the phase is on.
static_assert(s::decode_byte(0x80, 0x80 | 0x07, false)[0] == 7);  // phase off: ink
static_assert(s::decode_byte(0x80, 0x80 | 0x07, true)[0] == 0);   // phase on: swapped
static_assert(s::decode_byte(0x80, 0x07, true)[0] == 7);          // no FLASH bit: unaffected

// Palette: black, white, primaries (blue=1, red=2, green=4), BRIGHT = 8..15.
static_assert(s::to_rgb(0) == s::Rgb{0, 0, 0});
static_assert(s::to_rgb(7) == s::Rgb{200, 200, 200});
static_assert(s::to_rgb(15) == s::Rgb{255, 255, 255});
static_assert(s::to_rgb(1) == s::Rgb{0, 0, 200});    // blue
static_assert(s::to_rgb(2) == s::Rgb{200, 0, 0});    // red
static_assert(s::to_rgb(4) == s::Rgb{0, 200, 0});    // green

// FLASH phase toggles every 16 frames (32-frame cycle).
static_assert(s::flash_phase(0) == false);
static_assert(s::flash_phase(15) == false);
static_assert(s::flash_phase(16) == true);
static_assert(s::flash_phase(31) == true);
static_assert(s::flash_phase(32) == false);

} // namespace

int main() {
    std::cout << "Spectrum screen-decode verification\n"
                 "===================================\n";

    std::cout << "\n[1] Attribute decode\n";
    {
        const s::Attribute a = s::decode_attribute(0x80 | 0x40 | 0x38 | 0x05);
        check(a.ink == (5 | 8), "ink = 5 + BRIGHT = 13");
        check(a.paper == (7 | 8), "paper = 7 + BRIGHT = 15");
        check(a.flash, "FLASH set");
    }

    std::cout << "\n[2] Byte decode (MSB = leftmost pixel)\n";
    {
        // 0b10100000 with ink 7, paper 0.
        const auto px = s::decode_byte(0xA0, 0x07, false);
        check(px[0] == 7 && px[1] == 0 && px[2] == 7 && px[3] == 0,
              "bit pattern maps left-to-right to ink/paper");
        check(px[7] == 0, "trailing clear bits are paper");
    }

    std::cout << "\n[3] FLASH swap\n";
    {
        const uint8_t attr = 0x80 | 0x07;        // FLASH, ink 7, paper 0
        const auto off = s::decode_byte(0x80, attr, /*flash_on=*/false);
        const auto on  = s::decode_byte(0x80, attr, /*flash_on=*/true);
        check(off[0] == 7 && off[1] == 0, "phase off: ink/paper as written");
        check(on[0] == 0 && on[1] == 7, "phase on: ink/paper swapped");
    }

    std::cout << "\n[4] Line decode (32 bytes -> 256 pixels)\n";
    {
        std::array<uint8_t, 32> bitmap{};
        std::array<uint8_t, 32> attrs{};
        bitmap.fill(0xFF);          // every pixel set -> ink
        attrs.fill(0x07);           // ink 7, paper 0
        attrs[5] = 0x38;            // column 5: ink 0, paper 7 (so set bits -> 0)
        const auto line = s::decode_line(bitmap, attrs, false);

        check(line[0] == 7, "column 0 set pixel -> ink 7");
        check(line[255] == 7, "last column set pixel -> ink 7");
        check(line[5 * 8] == 0 && line[5 * 8 + 7] == 0,
              "column 5 (ink 0) renders its 8 pixels as 0");
    }

    std::cout << "\n[5] Palette\n";
    {
        check(s::kPalette.size() == 16, "16 colours");
        check(s::to_rgb(6) == s::Rgb{200, 200, 0}, "colour 6 = yellow (dim)");
        check(s::to_rgb(14) == s::Rgb{255, 255, 0}, "colour 14 = yellow (bright)");
    }

    std::cout << "\n===================================\n";
    if (failures == 0) {
        std::cout << "✅ ALL SCREEN-DECODE CHECKS PASSED\n";
        return 0;
    }
    std::cout << "❌ " << failures << " check(s) FAILED\n";
    return 1;
}
