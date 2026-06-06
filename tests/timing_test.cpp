//
// Z80 Digital Twin - Spectrum timing constants verification
// Copyright (c) 2025 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//
// Verifies the ULA clock tree (14/7/3.5 MHz) and that the frame/scanline
// geometry is internally consistent and derived from it.
//

#include "spectrum/timing.h"

#include <cmath>
#include <iostream>

namespace {

namespace t = z80::machine::spectrum::timing;

int failures = 0;
void check(bool ok, const char* what) {
    std::cout << (ok ? "  ✓ " : "  ✗ ") << what << '\n';
    if (!ok) ++failures;
}

} // namespace

int main() {
    std::cout << "Spectrum timing verification\n============================\n";

    std::cout << "\n[1] Clock tree (14 MHz /2 -> 7 MHz /2 -> 3.5 MHz)\n";
    check(t::kMasterHz == 14'000'000, "master = 14 MHz");
    check(t::kPixelHz == 7'000'000, "pixel = master/2 = 7 MHz");
    check(t::kCpuHz == 3'500'000, "cpu = master/4 = 3.5 MHz");
    check(t::kMasterPerT == 4, "1 T-state = 4 master cycles");
    check(t::kMasterPerPixel == 2, "1 pixel = 2 master cycles");
    check(t::kPixelsPerT == 2, "2 pixels per T-state");
    check(t::kCpuHz * t::kMasterPerT == t::kMasterHz, "cpu x 4 == master");
    check(t::kPixelHz * t::kMasterPerPixel == t::kMasterHz, "pixel x 2 == master");

    std::cout << "\n[2] Frame / scanline geometry\n";
    check(t::kTPerLine == 224, "224 T/scanline");
    check(t::kLines == 312, "312 scanlines/frame");
    check(t::kTPerFrame == 69888, "69,888 T/frame");
    check(t::kTPerLine * t::kLines == t::kTPerFrame, "T/line x lines == T/frame");
    check(t::kDisplayStartT == 14336, "display starts at T = 14,336");
    check(t::kTopBorderLines + t::kDisplayLines + t::kBottomBorderLines == t::kLines,
          "64 + 192 + 56 == 312 lines");
    check(std::abs(t::kFrameRateHz - 50.08) < 0.01, "field rate ~= 50.08 Hz");

    std::cout << "\n[3] Sub-T-state conversions\n";
    check(t::to_master(1) == 4 && t::to_master(224) == 896, "to_master(t) = 4t");
    check(t::to_pixels(1) == 2 && t::to_pixels(128) == 256, "to_pixels(t) = 2t (256 px in 128 T)");

    std::cout << "\n============================\n";
    if (failures == 0) {
        std::cout << "✅ ALL TIMING CHECKS PASSED\n";
        return 0;
    }
    std::cout << "❌ " << failures << " check(s) FAILED\n";
    return 1;
}
