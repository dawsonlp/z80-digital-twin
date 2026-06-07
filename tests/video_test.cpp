//
// Z80 Digital Twin - ZX Spectrum frame-render verification
// Copyright (c) 2025-2026 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//
// Verifies the frame renderer: the display-memory address mapping, border
// fill (uniform and per-line), display decode through the FrameSource seam, and
// the interleaved line-address mapping reflected in the output.
//

#include "spectrum/video.h"

#include <array>
#include <cstdint>
#include <iostream>

namespace {

namespace v = z80::machine::spectrum::video;

int failures = 0;
void check(bool ok, const char* what) {
    std::cout << (ok ? "  ✓ " : "  ✗ ") << what << '\n';
    if (!ok) ++failures;
}

// A final-memory source over a flat 64 KB image, with either a uniform border
// or a per-line border equal to (line & 7).
struct TestSource {
    std::array<uint8_t, 65536> mem{};
    uint8_t border_color = 0;
    bool per_line = false;

    uint8_t border_for_line(int rendered_line) const {
        return per_line ? static_cast<uint8_t>(rendered_line & 7) : border_color;
    }
    uint8_t screen_byte(uint16_t addr, int /*display_line*/) const { return mem[addr]; }
};

uint8_t pixel(const std::array<uint8_t, v::kFramePixels>& f, int x, int y) {
    return f[static_cast<std::size_t>(y) * v::kFrameWidth + x];
}

// -- Compile-time address mapping --------------------------------------------
static_assert(v::bitmap_address(0, 0) == 0x4000);
static_assert(v::bitmap_address(0, 5) == 0x4005);     // columns are contiguous
static_assert(v::bitmap_address(1, 0) == 0x4100);     // pixel-row within a char
static_assert(v::bitmap_address(8, 0) == 0x4020);     // next char row
static_assert(v::bitmap_address(64, 0) == 0x4800);    // next third
static_assert(v::attribute_address(0, 0) == 0x5800);
static_assert(v::attribute_address(8, 0) == 0x5820);
static_assert(v::attribute_address(64, 0) == 0x5900);
static_assert(v::kFrameWidth == 320 && v::kFrameHeight == 256);

} // namespace

int main() {
    std::cout << "Spectrum frame-render verification\n"
                 "==================================\n";

    std::cout << "\n[1] Uniform border + blank screen\n";
    {
        TestSource src;
        src.border_color = 5;     // cyan border; screen all zero -> black paper
        std::array<uint8_t, v::kFramePixels> frame{};
        v::render_frame(src, false, frame);

        check(pixel(frame, 0, 0) == 5, "top-left corner is border");
        bool top_uniform = true;
        for (int x = 0; x < v::kFrameWidth; ++x)
            if (pixel(frame, x, 0) != 5) top_uniform = false;
        check(top_uniform, "a top-border row is all border");
        check(pixel(frame, 0, v::kBorderTop) == 5, "left border of a display row");
        check(pixel(frame, v::kBorderLeft, v::kBorderTop) == 0,
              "display pixel is paper (0)");
    }

    std::cout << "\n[2] A set bitmap bit renders as ink\n";
    {
        TestSource src;
        src.border_color = 0;
        src.mem[v::attribute_address(0, 0)] = 0x07;   // ink 7, paper 0
        src.mem[v::bitmap_address(0, 0)]    = 0x80;   // leftmost pixel set
        std::array<uint8_t, v::kFramePixels> frame{};
        v::render_frame(src, false, frame);

        check(pixel(frame, v::kBorderLeft, v::kBorderTop) == 7, "set bit -> ink 7");
        check(pixel(frame, v::kBorderLeft + 1, v::kBorderTop) == 0, "clear bit -> paper 0");
    }

    std::cout << "\n[3] Per-line border (rainbow)\n";
    {
        TestSource src;
        src.per_line = true;      // border for rendered line r == (r & 7)
        std::array<uint8_t, v::kFramePixels> frame{};
        v::render_frame(src, false, frame);

        check(pixel(frame, 0, 3) == 3, "top-border line 3 has border 3");
        const int bottom = v::kBorderTop + v::kDisplayHeight + 5;   // a bottom-border line
        check(pixel(frame, 0, bottom) == (bottom & 7), "a bottom-border line uses its own colour");
        // A display row's left/right border still follows the per-line colour.
        check(pixel(frame, 0, v::kBorderTop + 4) == ((v::kBorderTop + 4) & 7),
              "display row's border follows the per-line colour");
    }

    std::cout << "\n[4] Interleaved line address reflected in output\n";
    {
        TestSource src;                                // char row 1 = display line 8
        src.mem[v::attribute_address(8, 0)] = 0x07;
        src.mem[v::bitmap_address(8, 0)]    = 0x80;
        std::array<uint8_t, v::kFramePixels> frame{};
        v::render_frame(src, false, frame);

        check(pixel(frame, v::kBorderLeft, v::kBorderTop + 8) == 7,
              "display line 8 byte renders on rendered row kBorderTop+8");
        check(pixel(frame, v::kBorderLeft, v::kBorderTop + 0) == 0,
              "display line 0 untouched (still paper)");
    }

    std::cout << "\n==================================\n";
    if (failures == 0) {
        std::cout << "✅ ALL FRAME-RENDER CHECKS PASSED\n";
        return 0;
    }
    std::cout << "❌ " << failures << " check(s) FAILED\n";
    return 1;
}
