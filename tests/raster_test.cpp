//
// Z80 Digital Twin - beam-accurate screen reconstruction verification
// Copyright (c) 2025 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//
// Drives the ULA's display-file write timeline directly with a controllable
// clock (no CPU): a byte written mid-frame must read as its pre-write value on
// scanlines the beam already passed, and its new value on later scanlines — the
// mechanism behind per-scanline multicolour / raster effects. Bytes not written
// this frame read straight from RAM.
//

#include "spectrum/ula.h"
#include "spectrum/timing.h"

#include <array>
#include <cstdint>
#include <iostream>

namespace {

using z80::machine::spectrum::Ula;
namespace t = z80::machine::spectrum::timing;

int failures = 0;
void check(bool ok, const char* what) {
    std::cout << (ok ? "  ✓ " : "  ✗ ") << what << '\n';
    if (!ok) ++failures;
}

// T-state at which display line L's bytes are fetched (the reconstruction cutoff).
constexpr uint32_t line_t(int line) {
    return static_cast<uint32_t>(t::kDisplayStartT) + static_cast<uint32_t>(line) * t::kTPerLine;
}

} // namespace

int main() {
    std::cout << "Beam-accurate screen verification\n=================================\n";

    std::array<uint8_t, 65536> ram{};
    ram[0x4000] = 0xAA;          // an untouched bitmap byte
    ram[0x5800] = 0x07;          // an attribute (will be rewritten below)

    uint64_t now = 0;
    Ula ula;
    ula.set_clock([&] { return now; });
    ula.set_reader([&](uint16_t a) { return ram[a]; });

    std::cout << "\n[1] Attribute changed mid-frame: each scanline sees its own value\n";
    {
        ula.begin_frame();
        // Frame-start value 0x01; change to 0x02 just before line 3, then to 0x03
        // just before line 11.
        now = line_t(3) - 50;  ula.on_write(0x5800, /*old=*/0x01, /*new=*/0x02);
        now = line_t(11) - 50; ula.on_write(0x5800, /*old=*/0x02, /*new=*/0x03);

        check(ula.screen_byte(0x5800, 0) == 0x01, "line 0: frame-start value (0x01)");
        check(ula.screen_byte(0x5800, 2) == 0x01, "line 2: still pre-change (0x01)");
        check(ula.screen_byte(0x5800, 3) == 0x02, "line 3: first change visible (0x02)");
        check(ula.screen_byte(0x5800, 10) == 0x02, "line 10: still second value (0x02)");
        check(ula.screen_byte(0x5800, 11) == 0x03, "line 11: second change visible (0x03)");
        check(ula.screen_byte(0x5800, 191) == 0x03, "last line: latest value (0x03)");
    }

    std::cout << "\n[2] Untouched bytes read straight from RAM\n";
    {
        check(ula.screen_byte(0x4000, 0) == 0xAA, "unwritten bitmap byte -> RAM value");
        check(ula.screen_byte(0x4000, 100) == 0xAA, "...same on every line");
    }

    std::cout << "\n[3] A write before the display area affects all scanlines\n";
    {
        ula.begin_frame();
        now = 1000;  // well before line 0's fetch (kDisplayStartT = 14336)
        ula.on_write(0x5801, 0x00, 0x38);
        check(ula.screen_byte(0x5801, 0) == 0x38, "line 0 sees the early write");
        check(ula.screen_byte(0x5801, 191) == 0x38, "line 191 sees it too");
    }

    std::cout << "\n[4] begin_frame() drops the previous frame's writes\n";
    {
        ula.begin_frame();
        check(ula.screen_byte(0x5800, 5) == ram[0x5800],
              "after begin_frame, 0x5800 reads RAM again (0x07)");
    }

    std::cout << "\n=================================\n";
    if (failures == 0) {
        std::cout << "✅ ALL BEAM-ACCURATE CHECKS PASSED\n";
        return 0;
    }
    std::cout << "❌ " << failures << " check(s) FAILED\n";
    return 1;
}
