//
// Z80 Digital Twin - floating-bus verification
// Copyright (c) 2025-2026 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//
// An IN from an undecoded (odd) port reads the byte the ULA is fetching for the
// display at that instant, and 0xFF when the beam is not in the active display
// fetch. Driven with a controllable clock (no CPU): plant sentinels in the
// display file and assert read_port() of an odd port follows the 8-T fetch
// pattern (bitmap, attr, bitmap, attr, then four idle slots) inside the 128-T
// display span, and idles to 0xFF on the border/retrace. See FLOATING_BUS_DESIGN.md.
//
// Locks the §4 fetch pattern independently of the calibration offsets, so a
// future kFloatingBusReadT re-tune can't silently change the pattern shape.
//

#include "spectrum/ula.h"
#include "spectrum/timing.h"
#include "spectrum/video.h"

#include <array>
#include <cstdint>
#include <iostream>

namespace {

using z80::machine::spectrum::Ula;
namespace t = z80::machine::spectrum::timing;
namespace v = z80::machine::spectrum::video;

int failures = 0;
void check(bool ok, const char* what) {
    std::cout << (ok ? "  ✓ " : "  ✗ ") << what << '\n';
    if (!ok) ++failures;
}

// Frame T-state at the start of display line L (absolute scanline 64 + L).
constexpr uint32_t line_start_t(int display_line) {
    return (t::kTopBorderLines + static_cast<uint32_t>(display_line)) * t::kTPerLine;
}

constexpr uint16_t kOddPort = 0x00FF;   // A0 = 1 => undecoded => floating bus

} // namespace

int main() {
    std::cout << "Floating-bus verification\n=========================\n";

    std::array<uint8_t, 0x10000> mem{};
    uint64_t now = 0;

    Ula ula;
    ula.set_clock([&] { return now; });
    ula.set_reader([&](uint16_t a) { return mem[a]; });
    // kFloatingBusReadT is assumed 0 here (uncalibrated default); these tests
    // assert the pattern at slot centres, which is robust to small offsets.

    // -- A display byte is read live on the bus during the fetch -------------
    constexpr int kLine = 100;
    const uint16_t bmp0 = v::bitmap_address(kLine, 0);
    const uint16_t att0 = v::attribute_address(kLine, 0);
    mem[bmp0] = 0x3C;
    mem[att0] = 0x47;

    now = line_start_t(kLine) + 0;   // slot 0 -> bitmap of cell 0
    check(ula.read_port(kOddPort) == 0x3C, "display slot 0 reads the bitmap byte");

    now = line_start_t(kLine) + 1;   // slot 1 -> attribute of cell 0
    check(ula.read_port(kOddPort) == 0x47, "display slot 1 reads the attribute byte");

    // -- Idle slots (4..7) of the 8-T pattern float to 0xFF ------------------
    bool idle_ok = true;
    for (uint32_t slot = 4; slot < 8; ++slot) {
        now = line_start_t(kLine) + slot;
        if (ula.read_port(kOddPort) != 0xFF) idle_ok = false;
    }
    check(idle_ok, "display idle slots 4..7 float to 0xFF");

    // -- Second cell of the pair (slots 2,3) maps to column 1 ----------------
    const uint16_t bmp1 = v::bitmap_address(kLine, 1);
    mem[bmp1] = 0x5A;
    now = line_start_t(kLine) + 2;   // slot 2 -> bitmap of cell 1
    check(ula.read_port(kOddPort) == 0x5A, "display slot 2 reads next cell's bitmap");

    // -- Last column (31) is fetched late in the line -----------------------
    const uint16_t bmp31 = v::bitmap_address(kLine, 31);
    mem[bmp31] = 0x18;
    now = line_start_t(kLine) + 15 * 8;   // pair k=15 -> cells 30,31; slot 0 = col 30
    mem[v::bitmap_address(kLine, 30)] = 0x99;
    check(ula.read_port(kOddPort) == 0x99, "display reaches cell 30 near end of line");
    now = line_start_t(kLine) + 15 * 8 + 2;   // slot 2 = col 31
    check(ula.read_port(kOddPort) == 0x18, "display reaches cell 31 (last column)");

    // -- Outside the active fetch the bus idles to 0xFF ----------------------
    now = (t::kTopBorderLines - 10) * t::kTPerLine;   // top border line
    check(ula.read_port(kOddPort) == 0xFF, "top border floats to 0xFF");

    now = line_start_t(kLine) + t::kTPerLine - 20;    // right border / retrace of a display line
    check(ula.read_port(kOddPort) == 0xFF, "horizontal border/retrace floats to 0xFF");

    const int below = static_cast<int>(t::kDisplayLines) + 5;
    now = line_start_t(below);                        // below the display
    check(ula.read_port(kOddPort) == 0xFF, "bottom border floats to 0xFF");

    // -- Even ports are the keyboard path, not the floating bus --------------
    // With no key held and no reader influence, the matrix read is 0xBF..0xFF;
    // the point is it does NOT route through floating_bus() (independent of clock).
    now = line_start_t(kLine);                        // a display T-state
    const uint8_t even = ula.read_port(0x00FE);
    check((even & 0x1F) == 0x1F, "even port reads the keyboard matrix (no key) not the bus");

    std::cout << (failures == 0 ? "\nAll floating-bus checks passed.\n"
                                : "\nFAILURES above.\n");
    return failures == 0 ? 0 : 1;
}
