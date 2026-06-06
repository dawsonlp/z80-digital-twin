//
// Z80 Digital Twin Debugger - Disassembler tests
// Copyright (c) 2025 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//
// Golden checks across every prefix (none/CB/ED/DD/FD/DDCB/FDCB), symbol
// resolution, and a length-coverage sweep over all 256 base opcodes.
//

#include "disassembler.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <optional>
#include <vector>

namespace {

using namespace z80::dbg;

int failures = 0;

// 64K backing store so any address / wraparound works.
std::array<uint8_t, 65536> g_mem{};
ByteReader reader() {
    return [](uint16_t a) { return g_mem[a]; };
}

void place(uint16_t at, const std::vector<uint8_t>& bytes) {
    for (size_t i = 0; i < bytes.size(); ++i)
        g_mem[static_cast<uint16_t>(at + i)] = bytes[i];
}

// Decode `bytes` at `at` and assert rendered text + length.
void expect(uint16_t at, const std::vector<uint8_t>& bytes,
            const char* text, uint8_t len,
            const SymbolResolver& resolve = {}) {
    place(at, bytes);
    Disassembler d;
    Instruction ins = d.Decode(reader(), at, resolve);
    const bool ok = ins.text == text && ins.length == len;
    std::cout << (ok ? "  ✓ " : "  ✗ ") << text;
    if (!ok) {
        std::cout << "   [got \"" << ins.text << "\" len " << int(ins.length)
                  << ", want len " << int(len) << "]";
        ++failures;
    }
    std::cout << '\n';
}

} // namespace

int main() {
    std::cout << "Disassembler tests\n==================\n";

    // --- Unprefixed -----------------------------------------------------------
    std::cout << "\n[base]\n";
    expect(0x0000, {0x00}, "NOP", 1);
    expect(0x0000, {0x3E, 0x42}, "LD A, 0x42", 2);
    expect(0x0000, {0x06, 0x0A}, "LD B, 0x0A", 2);
    expect(0x0000, {0x21, 0x00, 0x90}, "LD HL, 0x9000", 3);
    expect(0x0000, {0x32, 0x00, 0x90}, "LD (0x9000), A", 3);
    expect(0x0000, {0x3A, 0x00, 0x90}, "LD A, (0x9000)", 3);
    expect(0x0000, {0xC3, 0x34, 0x12}, "JP 0x1234", 3);
    expect(0x0000, {0xCD, 0x34, 0x12}, "CALL 0x1234", 3);
    expect(0x0000, {0xC2, 0x34, 0x12}, "JP NZ, 0x1234", 3);
    expect(0x0000, {0xC9}, "RET", 1);
    expect(0x0000, {0xC5}, "PUSH BC", 1);
    expect(0x0000, {0xF5}, "PUSH AF", 1);
    expect(0x0000, {0x80}, "ADD A, B", 1);
    expect(0x0000, {0x90}, "SUB B", 1);
    expect(0x0000, {0xA8}, "XOR B", 1);
    expect(0x0000, {0xFE, 0x05}, "CP 0x05", 2);
    expect(0x0000, {0xC6, 0x01}, "ADD A, 0x01", 2);
    expect(0x0000, {0xD3, 0x10}, "OUT (0x10), A", 2);
    expect(0x0000, {0xDB, 0x20}, "IN A, (0x20)", 2);
    expect(0x0000, {0xE9}, "JP (HL)", 1);
    expect(0x0000, {0x09}, "ADD HL, BC", 1);
    expect(0x0000, {0xEB}, "EX DE, HL", 1);
    expect(0x0000, {0x08}, "EX AF, AF'", 1);
    expect(0x0000, {0xFF}, "RST 0x38", 1);
    expect(0x0000, {0xC7}, "RST 0x00", 1);
    expect(0x0000, {0x76}, "HALT", 1);
    // Relative jumps render the resolved target address.
    expect(0x0002, {0x28, 0x0B}, "JR Z, 0x000F", 2);   // 0x0002+2+0x0B
    expect(0x000B, {0x18, 0xF3}, "JR 0x0000", 2);      // 0x000B+2-13

    // --- CB -------------------------------------------------------------------
    std::cout << "\n[CB]\n";
    expect(0x0000, {0xCB, 0x3F}, "SRL A", 2);
    expect(0x0000, {0xCB, 0x00}, "RLC B", 2);
    expect(0x0000, {0xCB, 0x47}, "BIT 0, A", 2);
    expect(0x0000, {0xCB, 0xC7}, "SET 0, A", 2);
    expect(0x0000, {0xCB, 0x86}, "RES 0, (HL)", 2);

    // --- ED -------------------------------------------------------------------
    std::cout << "\n[ED]\n";
    expect(0x0000, {0xED, 0x52}, "SBC HL, DE", 2);
    expect(0x0000, {0xED, 0x4A}, "ADC HL, BC", 2);
    expect(0x0000, {0xED, 0xB0}, "LDIR", 2);
    expect(0x0000, {0xED, 0xA0}, "LDI", 2);
    expect(0x0000, {0xED, 0x44}, "NEG", 2);
    expect(0x0000, {0xED, 0x56}, "IM 1", 2);
    expect(0x0000, {0xED, 0x57}, "LD A, I", 2);
    expect(0x0000, {0xED, 0x78}, "IN A, (C)", 2);
    expect(0x0000, {0xED, 0x79}, "OUT (C), A", 2);
    expect(0x0000, {0xED, 0x43, 0x00, 0x90}, "LD (0x9000), BC", 4);
    expect(0x0000, {0xED, 0x4B, 0x00, 0x90}, "LD BC, (0x9000)", 4);

    // --- DD (IX) --------------------------------------------------------------
    std::cout << "\n[DD/IX]\n";
    expect(0x0000, {0xDD, 0x21, 0x00, 0x90}, "LD IX, 0x9000", 4);
    expect(0x0000, {0xDD, 0x7E, 0x05}, "LD A, (IX+0x05)", 3);
    expect(0x0000, {0xDD, 0x77, 0xFB}, "LD (IX-0x05), A", 3);  // 0xFB = -5
    expect(0x0000, {0xDD, 0x36, 0x02, 0x42}, "LD (IX+0x02), 0x42", 4);
    expect(0x0000, {0xDD, 0x09}, "ADD IX, BC", 2);
    expect(0x0000, {0xDD, 0x86, 0x05}, "ADD A, (IX+0x05)", 3);
    expect(0x0000, {0xDD, 0xE5}, "PUSH IX", 2);
    expect(0x0000, {0xDD, 0xE9}, "JP (IX)", 2);
    // (HL)-memory present: the *other* register stays H/L (not IXH/IXL).
    expect(0x0000, {0xDD, 0x66, 0x00}, "LD H, (IX+0x00)", 3);
    // No memory operand: H/L become IXH/IXL (undocumented halves).
    expect(0x0000, {0xDD, 0x60}, "LD IXH, B", 2);
    expect(0x0000, {0xDD, 0x44}, "LD B, IXH", 2);

    // --- FD (IY) --------------------------------------------------------------
    std::cout << "\n[FD/IY]\n";
    expect(0x0000, {0xFD, 0x21, 0x00, 0x90}, "LD IY, 0x9000", 4);
    expect(0x0000, {0xFD, 0x7E, 0x05}, "LD A, (IY+0x05)", 3);

    // --- DD CB / FD CB --------------------------------------------------------
    std::cout << "\n[DDCB/FDCB]\n";
    expect(0x0000, {0xDD, 0xCB, 0x05, 0x06}, "RLC (IX+0x05)", 4);
    expect(0x0000, {0xDD, 0xCB, 0x05, 0x46}, "BIT 0, (IX+0x05)", 4);
    expect(0x0000, {0xDD, 0xCB, 0xFB, 0xC6}, "SET 0, (IX-0x05)", 4);
    expect(0x0000, {0xFD, 0xCB, 0x05, 0x46}, "BIT 0, (IY+0x05)", 4);

    // --- Symbol resolution ----------------------------------------------------
    std::cout << "\n[symbols]\n";
    SymbolResolver resolve = [](uint16_t a) -> std::optional<std::string> {
        if (a == 0x1234) return std::string("MAIN");
        if (a == 0x000F) return std::string("DONE");
        return std::nullopt;
    };
    expect(0x0000, {0xC3, 0x34, 0x12}, "JP MAIN", 3, resolve);
    expect(0x0000, {0xCD, 0x34, 0x12}, "CALL MAIN", 3, resolve);
    expect(0x0002, {0x28, 0x0B}, "JR Z, DONE", 2, resolve);

    // --- Branch targets (powers right-click "Go to target") ------------------
    std::cout << "\n[branch targets]\n";
    {
        Disassembler d;
        auto bt = [&](uint16_t at, const std::vector<uint8_t>& bytes) {
            place(at, bytes);
            return d.Decode(reader(), at).branch_target;
        };
        auto has = [&](const char* what, std::optional<uint16_t> got, uint16_t want) {
            const bool ok = got.has_value() && *got == want;
            std::cout << (ok ? "  ✓ " : "  ✗ ") << what;
            if (!ok) { std::cout << " [got " << (got ? int(*got) : -1) << "]"; ++failures; }
            std::cout << '\n';
        };
        auto none = [&](const char* what, std::optional<uint16_t> got) {
            const bool ok = !got.has_value();
            std::cout << (ok ? "  ✓ " : "  ✗ ") << what << '\n';
            if (!ok) ++failures;
        };
        has("JP 0x1234",        bt(0x0000, {0xC3, 0x34, 0x12}), 0x1234);
        has("JP NZ, 0x1234",    bt(0x0000, {0xC2, 0x34, 0x12}), 0x1234);
        has("CALL 0x2000",      bt(0x0000, {0xCD, 0x00, 0x20}), 0x2000);
        has("CALL NC, 0x2000",  bt(0x0000, {0xD4, 0x00, 0x20}), 0x2000);
        has("JR  (rel +0x0B)",  bt(0x0002, {0x18, 0x0B}),       0x000F);
        has("DJNZ (rel -2)",    bt(0x0010, {0x10, 0xFE}),       0x0010);
        has("RST 0x18",         bt(0x0000, {0xDF}),             0x0018);
        none("RET has no target",     bt(0x0000, {0xC9}));
        none("JP (HL) has no target", bt(0x0000, {0xE9}));
        none("LD A,B has no target",  bt(0x0000, {0x78}));
    }

    // --- Length coverage: all 256 base opcodes (excluding prefix bytes) -------
    std::cout << "\n[length sweep over base opcodes]\n";
    {
        std::array<uint8_t, 256> want{};
        want.fill(1);
        const uint8_t two[] = {0x06,0x0E,0x16,0x1E,0x26,0x2E,0x36,0x3E,
                               0xC6,0xCE,0xD6,0xDE,0xE6,0xEE,0xF6,0xFE,
                               0xD3,0xDB,0x10,0x18,0x20,0x28,0x30,0x38};
        const uint8_t three[] = {0x01,0x11,0x21,0x31,0x22,0x2A,0x32,0x3A,
                                 0xC2,0xC3,0xC4,0xCA,0xCC,0xCD,0xD2,0xD4,
                                 0xDA,0xDC,0xE2,0xE4,0xEA,0xEC,0xF2,0xF4,
                                 0xFA,0xFC};
        for (uint8_t b : two)   want[b] = 2;
        for (uint8_t b : three) want[b] = 3;

        Disassembler d;
        int mismatches = 0;
        for (int op = 0; op < 256; ++op) {
            if (op == 0xCB || op == 0xDD || op == 0xED || op == 0xFD) continue;
            place(0x0100, {static_cast<uint8_t>(op), 0x00, 0x00});
            Instruction ins = d.Decode(reader(), 0x0100);
            if (ins.length != want[op] || ins.text.empty()) {
                std::cout << "  ✗ opcode 0x" << std::hex << op << std::dec
                          << " -> \"" << ins.text << "\" len " << int(ins.length)
                          << " (want " << int(want[op]) << ")\n";
                ++mismatches;
            }
        }
        if (mismatches == 0) std::cout << "  ✓ all 252 base opcodes have expected length\n";
        else failures += mismatches;
    }

    std::cout << "\n==================\n";
    if (failures == 0) {
        std::cout << "✅ ALL DISASSEMBLER CHECKS PASSED\n";
        return 0;
    }
    std::cout << "❌ " << failures << " check(s) FAILED\n";
    return 1;
}
