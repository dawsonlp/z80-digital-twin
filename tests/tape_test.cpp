//
// Z80 Digital Twin - tape (.tap / .tzx) signal verification
// Copyright (c) 2025-2026 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//
// Verifies the pulse-train generation and EAR-level playback deterministically
// (no CPU): .tap block parsing, the pilot/sync/data pulse structure, that the EAR
// level toggles on each pulse boundary so the ROM's edge timing sees the signal,
// and that .tzx images parse to the same pulse train (standard block 0x10) while
// skipping metadata blocks and auto-detecting the format.
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

// Append a little-endian 16-bit value.
void put16(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(static_cast<uint8_t>(x & 0xFF));
    v.push_back(static_cast<uint8_t>((x >> 8) & 0xFF));
}

// A minimal valid .tzx header: "ZXTape!" + 0x1A + version 1.20.
std::vector<uint8_t> tzx_header() {
    return {'Z', 'X', 'T', 'a', 'p', 'e', '!', 0x1A, 1, 20};
}

// A standard-speed data block (0x10): [id][pause:2][len:2][data].
void put_block_0x10(std::vector<uint8_t>& v, uint32_t pause_ms,
                    const std::vector<uint8_t>& data) {
    v.push_back(0x10);
    put16(v, pause_ms);
    put16(v, static_cast<uint32_t>(data.size()));
    v.insert(v.end(), data.begin(), data.end());
}

} // namespace

int main() {
    std::cout << "Tape (.tap / .tzx) signal verification\n======================================\n";

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

    std::cout << "\n[4] TZX standard block (0x10) matches the .tap pulse train\n";
    {
        const std::vector<uint8_t> data = {0xFF, 0xAA, 0x55};

        // Reference .tap (default 1000 ms pause).
        Tape tap;
        tap.load_tap(make_tap({data}));

        // Equivalent .tzx: header + one 0x10 block with the same 1000 ms pause.
        auto tzx = tzx_header();
        put_block_0x10(tzx, /*pause_ms=*/1000, data);
        Tape tzxt;
        check(tzxt.load_tzx(tzx), "loaded via load_tzx");
        check(tzxt.block_count() == 1, "one block");
        check(tzxt.pulse_count() == tap.pulse_count(), "pulse count == .tap");
        check(tzxt.total_tstates() == tap.total_tstates(), "T-state total == .tap");
    }

    std::cout << "\n[5] Auto-detect + metadata skipping\n";
    {
        auto tzx = tzx_header();
        // Archive-info block (0x32) we must skip: [id][len:2][payload].
        const std::vector<uint8_t> info = {0x00, 0x05, 'h', 'e', 'l', 'l', 'o'};
        tzx.push_back(0x32);
        put16(tzx, static_cast<uint32_t>(info.size()));
        tzx.insert(tzx.end(), info.begin(), info.end());
        put_block_0x10(tzx, 1000, {0x00, 0x01});   // header block: 2 bytes

        Tape tape;
        check(tape.load(tzx), "load() auto-detects .tzx");
        check(tape.block_count() == 1, "metadata skipped, one data block");
        // 8063 pilot + 2 sync + 2*16 data + 1 pause
        check(tape.pulse_count() == 8063u + 2u + 32u + 1u, "header-block pulse count");
    }
    {
        // load() routes a raw .tap (no magic) to the .tap parser.
        Tape tape;
        check(tape.load(make_tap({{0xFF, 0x00}})), "load() auto-detects .tap");
        check(tape.block_count() == 1, "tap block parsed via load()");
    }

    std::cout << "\n======================================\n";
    if (failures == 0) {
        std::cout << "✅ ALL TAPE CHECKS PASSED\n";
        return 0;
    }
    std::cout << "❌ " << failures << " check(s) FAILED\n";
    return 1;
}
