//
// Z80 Digital Twin - ZX Spectrum keyboard matrix
// Copyright (c) 2025-2026 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//
// The 48K keyboard is an 8x5 matrix read through the ULA's even ports. The high
// byte of the port address selects half-rows: address line A(8+r) LOW selects
// half-row r, and data bits D0..D4 read that row's keys (0 = pressed). The ROM
// scans with B = 0xFE, 0xFD, 0xFB, 0xF7, 0xEF, 0xDF, 0xBF, 0x7F (one zero bit
// each) and C = 0xFE.
//
// This header is the hardware *layout* only (which key sits at which row/bit),
// independent of any host toolkit. The matrix state and the IN decode live in
// the ULA; the host-key mapping lives in the viewer.
//
//   half-row 0 (A8 , 0xFE): CAPS SHIFT  Z  X  C  V
//   half-row 1 (A9 , 0xFD): A  S  D  F  G
//   half-row 2 (A10, 0xFB): Q  W  E  R  T
//   half-row 3 (A11, 0xF7): 1  2  3  4  5
//   half-row 4 (A12, 0xEF): 0  9  8  7  6
//   half-row 5 (A13, 0xDF): P  O  I  U  Y
//   half-row 6 (A14, 0xBF): ENTER  L  K  J  H
//   half-row 7 (A15, 0x7F): SPACE  SYM SHIFT  M  N  B
//

#ifndef Z80_MACHINE_SPECTRUM_KEYBOARD_H
#define Z80_MACHINE_SPECTRUM_KEYBOARD_H

#include <cstdint>

namespace z80::machine::spectrum::keyboard {

/// @brief A position in the keyboard matrix: half-row 0..7, data bit 0..4.
struct Key {
    uint8_t half_row;
    uint8_t bit;

    [[nodiscard]] constexpr bool valid() const noexcept { return half_row < 8 && bit < 5; }
    friend constexpr bool operator==(const Key&, const Key&) = default;
};

inline constexpr Key kNone{0xFF, 0xFF};

// The two shift keys and the two "always there" keys.
inline constexpr Key kCapsShift  {0, 0};
inline constexpr Key kSymbolShift {7, 1};
inline constexpr Key kEnter      {6, 0};
inline constexpr Key kSpace      {7, 0};

/// @brief ASCII letter/digit -> matrix position (uppercase letters, '0'..'9').
struct AsciiKey {
    char c;
    uint8_t half_row;
    uint8_t bit;
};

inline constexpr AsciiKey kAsciiKeys[] = {
    {'A', 1, 0}, {'S', 1, 1}, {'D', 1, 2}, {'F', 1, 3}, {'G', 1, 4},
    {'Q', 2, 0}, {'W', 2, 1}, {'E', 2, 2}, {'R', 2, 3}, {'T', 2, 4},
    {'P', 5, 0}, {'O', 5, 1}, {'I', 5, 2}, {'U', 5, 3}, {'Y', 5, 4},
    {'L', 6, 1}, {'K', 6, 2}, {'J', 6, 3}, {'H', 6, 4},
    {'M', 7, 2}, {'N', 7, 3}, {'B', 7, 4},
    {'Z', 0, 1}, {'X', 0, 2}, {'C', 0, 3}, {'V', 0, 4},
    {'1', 3, 0}, {'2', 3, 1}, {'3', 3, 2}, {'4', 3, 3}, {'5', 3, 4},
    {'0', 4, 0}, {'9', 4, 1}, {'8', 4, 2}, {'7', 4, 3}, {'6', 4, 4},
};

/// @brief Matrix position of an uppercase letter or digit, or kNone.
[[nodiscard]] constexpr Key key_for_ascii(char c) noexcept {
    for (const AsciiKey& k : kAsciiKeys)
        if (k.c == c) return Key{k.half_row, k.bit};
    return kNone;
}

} // namespace z80::machine::spectrum::keyboard

#endif // Z80_MACHINE_SPECTRUM_KEYBOARD_H
