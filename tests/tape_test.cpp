//
// Z80 Digital Twin - tape (.tap) signal verification
// Copyright (c) 2025 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//
// Verifies the .tap pulse-train generation and EAR-level playback deterministically
// (no CPU): block parsing, the pilot/sync/data pulse structure, and that the EAR
// level toggles on each pulse boundary so the ROM's edge timing sees the signal.
//

#include "spectrum/tape.h"

#include <cstdint>
#include <iostream>
#include <vector>

namespace {

using z80::machine::spectrum::Tape;

int failures = 0;
void check(bool ok, const char* what) {
    std::cout << (ok ? "  ✓ " : "  ✗ ") << what << '\n';
    if (!ok) ++failures;
}

// Build a .tap: each block is [len:2 LE][data...].
std::vector<uint8_t> make_tap(std::initializer_list<std::vector<uint8_t>> blocks) {
    std::vector<uint8_t> tap;
    for (const auto& b : blocks) {
        tap.push_back(static_cast<uint8_t>(b.size() & 0xFF));
        tap.push_back(static_cast<uint8_t>((b.size() >> 8) & 0xFF));
        tap.insert(tap.end(), b.begin(), b.end());
    }
    return tap;
}

} // namespace

int main() {
    std::cout << "Tape (.tap) signal verification\n===============================\n";

    std::cout << "\n[1] Block parse + pulse structure\n";
    {
        // One data block (flag 0xFF -> data pilot) of 3 bytes.
        const auto tap = make_tap({{0xFF, 0xAA, 0x55}});
        Tape tape;
        check(tape.load_tap(tap), "loaded");
        check(tape.block_count() == 1, "one block");
        // 3223 pilot + 2 sync + 3*16 data + 1 pause
        check(tape.pulse_count() == 3223u + 2u + 48u + 1u, "data-block pulse count");
    }
    {
        // One header block (flag 0x00 -> header pilot 8063).
        const auto tap = make_tap({{0x00}});
        Tape tape;
        tape.load_tap(tap);
        check(tape.pulse_count() == 8063u + 2u + 16u + 1u, "header-block pulse count");
    }
    {
        const auto tap = make_tap({{0x00, 0x01}, {0xFF, 0x02, 0x03}});
        Tape tape;
        tape.load_tap(tap);
        check(tape.block_count() == 2, "two blocks parsed");
    }

    std::cout << "\n[2] EAR toggles every pilot pulse (2168 T)\n";
    {
        Tape tape;
        tape.load_tap(make_tap({{0xFF, 0x00}}));
        tape.play(0);
        check(tape.ear_level(0) == false, "pulse 0: low");
        check(tape.ear_level(Tape::kPilotPulse - 1) == false, "still pulse 0 at 2167");
        check(tape.ear_level(Tape::kPilotPulse) == true, "pulse 1: high at 2168");
        check(tape.ear_level(2 * Tape::kPilotPulse) == false, "pulse 2: low at 4336");
        check(tape.ear_level(3 * Tape::kPilotPulse) == true, "pulse 3: high at 6504");
    }

    std::cout << "\n[3] Idle high when stopped / before play\n";
    {
        Tape tape;
        tape.load_tap(make_tap({{0xFF, 0x00}}));
        check(tape.ear_level(0) == true, "not playing -> idle high");
        tape.play(0);
        check(tape.ear_level(0) == false, "playing -> follows signal");
        tape.stop();
        check(tape.ear_level(0) == true, "stopped -> idle high again");
    }

    std::cout << "\n===============================\n";
    if (failures == 0) {
        std::cout << "✅ ALL TAPE CHECKS PASSED\n";
        return 0;
    }
    std::cout << "❌ " << failures << " check(s) FAILED\n";
    return 1;
}
