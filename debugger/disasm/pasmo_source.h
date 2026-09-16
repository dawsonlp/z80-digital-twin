#ifndef Z80_DBG_PASMO_SOURCE_H
#define Z80_DBG_PASMO_SOURCE_H

#include "disassembler.h"
#include <cstdint>
#include <span>
#include <string>

namespace z80::dbg {

struct PasmoStatement {
    Instruction instruction;
    std::string source;
    bool raw_bytes = false;
};
// Decode/render one bounded statement using structured address resolution.
PasmoStatement DisassemblePasmoStatement(std::span<const uint8_t> bytes, uint16_t origin,
                                         const SymbolResolver& resolve = {});

// Linear source reconstruction of a non-wrapping range of 1..65535 bytes.
// No code/data inference or symbol recovery is implied. Non-canonical encodings
// retain bytes. Pasmo 0.5.5 cannot emit empty or full-64-KB raw binaries faithfully.
[[nodiscard]] std::string DisassemblePasmo(std::span<const uint8_t> bytes,
                                          uint16_t origin);

} // namespace z80::dbg
#endif
