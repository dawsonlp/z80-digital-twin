//
// Z80 Digital Twin Debugger - Disassembler
// Copyright (c) 2025-2026 Larry Dawson
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
#include <vector>

namespace z80::dbg {

/// @brief Reads one byte of program memory at a 16-bit address (wraps at 64K).
using ByteReader = std::function<uint8_t(uint16_t)>;

/// @brief Maps an absolute address to a label, or nullopt for "no symbol".
using SymbolResolver = std::function<std::optional<std::string>(uint16_t)>;

struct AddressOperand {
    enum class Use { Branch, Memory } use = Use::Memory;
    uint16_t target = 0;
};

/// @brief A decoded instruction.
struct Instruction {
    uint16_t address = 0;              ///< Address it was decoded at.
    uint32_t length = 1;               ///< Bytes consumed, including repeated prefixes.
    std::array<uint8_t, 4> bytes{};     ///< First min(length, 4) raw bytes for display.
    bool complete = true;             ///< False when the supplied byte range ends mid-instruction.
    std::string mnemonic;              ///< Operation, e.g. "LD", "ADD", "BIT".
    std::string operands;              ///< Operands, e.g. "A, (IX+0x05)" ("" if none).
    std::string text;                  ///< Rendered line: mnemonic [+ ' ' + operands].
    std::vector<std::string> symbols_used;  ///< Symbol names substituted into operands.
    std::optional<AddressOperand> address_operand; ///< Structured direct address use; immediates stay unclassified.
    std::optional<uint16_t> branch_target;  ///< Static target of a direct JP/JR/CALL/DJNZ/RST.
};

class Disassembler {
public:
    /// @brief Decode the instruction beginning at @p address.
    /// @param read    Byte accessor over program memory (e.g. cpu.ReadMemory).
    /// @param address Address of the first opcode byte.
    /// @param resolve Optional address->label resolver (defaults to none).
    /// @param available Bound reads to this many bytes (1..65536); incomplete
    ///        input is reported explicitly, including an all-prefix memory image.
    [[nodiscard]] Instruction Decode(const ByteReader& read, uint16_t address,
                                     const SymbolResolver& resolve = {},
                                     uint32_t available = 65536) const;

    /// @brief Byte length of the instruction at @p address (for step-over etc.).
    [[nodiscard]] uint32_t InstructionLength(const ByteReader& read,
                                            uint16_t address) const {
        return Decode(read, address).length;
    }
};

} // namespace z80::dbg

#endif // Z80_DBG_DISASSEMBLER_H
