//
// Z80 Digital Twin - ZX Spectrum keyboard verification
// Copyright (c) 2025-2026 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//
// Verifies the matrix layout (keyboard.h) and the ULA's active-low half-row
// IN decode: which port high byte selects which half-row, that pressed keys
// pull their data bit low, that unselected rows read high, and that selecting
// several rows ANDs them together.
//

#include "spectrum/keyboard.h"
#include "spectrum/ula.h"

#include <cstdint>
#include <iostream>

namespace {

namespace kb = z80::machine::spectrum::keyboard;
using z80::machine::spectrum::Ula;

int failures = 0;
void check(bool ok, const char* what) {
    std::cout << (ok ? "  ✓ " : "  ✗ ") << what << '\n';
    if (!ok) ++failures;
}

// Port whose high byte selects exactly half-row r (that address line low), C=0xFE.
constexpr uint16_t row_port(int r) {
    return static_cast<uint16_t>(((0xFF & ~(1u << r)) << 8) | 0xFE);
}

// -- Layout (compile-time) ---------------------------------------------------
static_assert(kb::key_for_ascii('A') == kb::Key{1, 0});
static_assert(kb::key_for_ascii('G') == kb::Key{1, 4});
static_assert(kb::key_for_ascii('Q') == kb::Key{2, 0});
static_assert(kb::key_for_ascii('1') == kb::Key{3, 0});
static_assert(kb::key_for_ascii('5') == kb::Key{3, 4});
static_assert(kb::key_for_ascii('0') == kb::Key{4, 0});
static_assert(kb::key_for_ascii('6') == kb::Key{4, 4});
static_assert(kb::key_for_ascii('B') == kb::Key{7, 4});
static_assert(kb::key_for_ascii('!') == kb::kNone);
static_assert(kb::kCapsShift == kb::Key{0, 0});
static_assert(kb::kSpace == kb::Key{7, 0});

} // namespace

int main() {
    std::cout << "Spectrum keyboard verification\n==============================\n";

    std::cout << "\n[1] No keys: every row reads high\n";
    {
        Ula ula;
        check(ula.read_port(0xFEFE) == 0xFF, "row 0 reads 0xFF");
        check(ula.read_port(0x7FFE) == 0xFF, "row 7 reads 0xFF");
        check(ula.read_port(0x00FE) == 0xFF, "all rows selected -> 0xFF");
        check(ula.read_port(0x00FF) == 0xFF, "odd port floats (0xFF)");
    }

    std::cout << "\n[2] A pressed (half-row 1, bit 0)\n";
    {
        Ula ula;
        const kb::Key a = kb::key_for_ascii('A');
        ula.key_down(a.half_row, a.bit);
        check((ula.read_port(row_port(1)) & 0x1F) == 0x1E, "row 1 select: bit 0 low");
        check((ula.read_port(row_port(0)) & 0x1F) == 0x1F, "row 0 select: unaffected");
        check((ula.read_port(0x00FE) & 0x1F) == 0x1E, "all-rows select sees A (AND)");
        ula.key_up(a.half_row, a.bit);
        check((ula.read_port(row_port(1)) & 0x1F) == 0x1F, "release restores high");
    }

    std::cout << "\n[3] SPACE + ENTER (rows 7 and 6, bit 0)\n";
    {
        Ula ula;
        ula.key_down(kb::kSpace.half_row, kb::kSpace.bit);
        ula.key_down(kb::kEnter.half_row, kb::kEnter.bit);
        check((ula.read_port(row_port(7)) & 0x01) == 0, "SPACE low on row 7");
        check((ula.read_port(row_port(6)) & 0x01) == 0, "ENTER low on row 6");
        check((ula.read_port(row_port(0)) & 0x01) == 1, "row 0 bit 0 still high");
    }

    std::cout << "\n[4] Bits 5..7 high (EAR), even-port only\n";
    {
        Ula ula;
        check((ula.read_port(0xFEFE) & 0xE0) == 0xE0, "high bits set (EAR high)");
    }

    std::cout << "\n==============================\n";
    if (failures == 0) {
        std::cout << "✅ ALL KEYBOARD CHECKS PASSED\n";
        return 0;
    }
    std::cout << "❌ " << failures << " check(s) FAILED\n";
    return 1;
}
