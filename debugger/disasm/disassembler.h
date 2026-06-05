//
// Z80 Digital Twin Debugger - Disassembler
// Copyright (c) 2025 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//
// A stateless Z80 instruction decoder. It turns the bytes at an address into
// {length, mnemonic, operands} using the classic octal (x/y/z/p/q) decoding
// algorithm, covering every prefix the CPU implements: none, CB, ED, DD, FD,
// DD CB, FD CB. It mirrors this CPU's IX/IY semantics exactly (HL->IX,
// (HL)->(IX+d) consuming a displacement, H/L->IXH/IXL only when no memory
// operand is present).
//
// Symbol resolution is intentionally decoupled: the decoder takes an optional
// SymbolResolver (address -> label) so it can be built and tested with no
// dependency on the symbol table. With no resolver, addresses render as hex.
//

#ifndef Z80_DBG_DISASSEMBLER_H
#define Z80_DBG_DISASSEMBLER_H

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace z80::dbg {

/// @brief Reads one byte of program memory at a 16-bit address (wraps at 64K).
using ByteReader = std::function<uint8_t(uint16_t)>;

/// @brief Maps an absolute address to a label, or nullopt for "no symbol".
using SymbolResolver = std::function<std::optional<std::string>(uint16_t)>;

/// @brief A decoded instruction.
struct Instruction {
    uint16_t address = 0;              ///< Address it was decoded at.
    uint8_t length = 1;                ///< Byte length (1..4).
    std::array<uint8_t, 4> bytes{};    ///< Raw bytes (first `length` valid).
    std::string mnemonic;              ///< Operation, e.g. "LD", "ADD", "BIT".
    std::string operands;              ///< Operands, e.g. "A, (IX+0x05)" ("" if none).
    std::string text;                  ///< Rendered line: mnemonic [+ ' ' + operands].
};

class Disassembler {
public:
    /// @brief Decode the instruction beginning at @p address.
    /// @param read    Byte accessor over program memory (e.g. cpu.ReadMemory).
    /// @param address Address of the first opcode byte.
    /// @param resolve Optional address->label resolver (defaults to none).
    [[nodiscard]] Instruction Decode(const ByteReader& read, uint16_t address,
                                     const SymbolResolver& resolve = {}) const;

    /// @brief Byte length of the instruction at @p address (for step-over etc.).
    [[nodiscard]] uint8_t InstructionLength(const ByteReader& read,
                                            uint16_t address) const {
        return Decode(read, address).length;
    }
};

} // namespace z80::dbg

#endif // Z80_DBG_DISASSEMBLER_H
