//
// Z80 Digital Twin Debugger - Disassembler implementation
// Copyright (c) 2025 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//
// Octal (x/y/z/p/q) decoding. References the well-known Z80 decoding scheme;
// DD/FD substitution is matched to this CPU's GetEffective* helpers.
//

#include "disassembler.h"

#include <format>

namespace z80::dbg {
namespace {

// -- Operand name tables (octal decode) --------------------------------------
constexpr const char* kR[8]   = {"B", "C", "D", "E", "H", "L", "(HL)", "A"};
constexpr const char* kRP[4]  = {"BC", "DE", "HL", "SP"};
constexpr const char* kRP2[4] = {"BC", "DE", "HL", "AF"};
constexpr const char* kCC[8]  = {"NZ", "Z", "NC", "C", "PO", "PE", "P", "M"};
constexpr const char* kROT[8] = {"RLC", "RRC", "RL", "RR", "SLA", "SRA", "SLL", "SRL"};
constexpr const char* kALU[8] = {"ADD", "ADC", "SUB", "SBC", "AND", "XOR", "OR", "CP"};
constexpr bool        kALU_A[8] = {true, true, false, true, false, false, false, false};
constexpr const char* kIM[8]  = {"0", "0", "1", "2", "0", "0", "1", "2"};

// Index-register mode for DD/FD prefixes.
enum class Index { None, IX, IY };

struct IndexNames {
    const char* full;  // "IX" / "IY" / "HL"
    const char* high;  // "IXH" / "IYH" / "H"
    const char* low;   // "IXL" / "IYL" / "L"
};

IndexNames names_for(Index ix) {
    switch (ix) {
        case Index::IX: return {"IX", "IXH", "IXL"};
        case Index::IY: return {"IY", "IYH", "IYL"};
        default:        return {"HL", "H", "L"};
    }
}

// Sequential byte cursor: reads in address order, recording up to 4 raw bytes.
struct Cursor {
    const ByteReader& read;
    uint16_t base;
    uint8_t n = 0;
    std::array<uint8_t, 4> bytes{};

    uint8_t next() {
        const uint8_t b = read(static_cast<uint16_t>(base + n));
        if (n < bytes.size()) bytes[n] = b;
        ++n;
        return b;
    }
    uint8_t  imm8()  { return next(); }
    uint16_t imm16() { const uint8_t lo = next(); const uint8_t hi = next();
                       return static_cast<uint16_t>(lo | (hi << 8)); }
    int8_t   disp()  { return static_cast<int8_t>(next()); }
};

// -- Formatting helpers ------------------------------------------------------
std::string hex8(uint8_t v)   { return std::format("0x{:02X}", v); }
std::string hex16(uint16_t v) { return std::format("0x{:04X}", v); }

std::string addr(uint16_t a, const SymbolResolver& resolve) {
    if (resolve) {
        if (auto label = resolve(a)) return *label;
    }
    return hex16(a);
}
std::string mem_abs(uint16_t a, const SymbolResolver& resolve) {
    return "(" + addr(a, resolve) + ")";
}

std::string index_mem(const IndexNames& nm, int8_t d) {
    const int v = d;
    return v >= 0 ? std::format("({}+0x{:02X})", nm.full, v)
                  : std::format("({}-0x{:02X})", nm.full, -v);
}

// Render r[idx]. idx 6 is (HL): under an index prefix it becomes (IX+d)/(IY+d)
// and consumes a displacement byte. allow_half controls IXH/IXL substitution
// for H/L (suppressed when the instruction also has a memory operand).
std::string reg(uint8_t idx, Index ix, bool allow_half, Cursor& cur) {
    const IndexNames nm = names_for(ix);
    switch (idx) {
        case 4: return (ix != Index::None && allow_half) ? nm.high : "H";
        case 5: return (ix != Index::None && allow_half) ? nm.low  : "L";
        case 6: return (ix != Index::None) ? index_mem(nm, cur.disp()) : "(HL)";
        default: return kR[idx];
    }
}

const char* rp(uint8_t p, Index ix) {
    return (p == 2 && ix != Index::None) ? names_for(ix).full : kRP[p];
}
const char* rp2(uint8_t p, Index ix) {
    return (p == 2 && ix != Index::None) ? names_for(ix).full : kRP2[p];
}

void finish(Instruction& out, std::string mnem, std::string ops = "") {
    out.mnemonic = std::move(mnem);
    out.operands = std::move(ops);
    out.text = out.operands.empty() ? out.mnemonic
                                    : out.mnemonic + " " + out.operands;
}

// -- CB-prefixed (rotate/shift/bit) ------------------------------------------
void decode_cb(Cursor& cur, Instruction& out) {
    const uint8_t op = cur.next();
    const uint8_t x = op >> 6, y = (op >> 3) & 7, z = op & 7;
    const std::string r = reg(z, Index::None, true, cur);  // never index here
    switch (x) {
        case 0: finish(out, kROT[y], r); break;
        case 1: finish(out, "BIT", std::format("{}, {}", y, r)); break;
        case 2: finish(out, "RES", std::format("{}, {}", y, r)); break;
        default: finish(out, "SET", std::format("{}, {}", y, r)); break;
    }
}

// -- DD CB / FD CB : operations on (IX+d)/(IY+d) -----------------------------
void decode_index_cb(Cursor& cur, Index ix, Instruction& out) {
    const int8_t d = cur.disp();      // displacement precedes the sub-opcode
    const uint8_t op = cur.next();
    const uint8_t x = op >> 6, y = (op >> 3) & 7;
    const std::string m = index_mem(names_for(ix), d);
    switch (x) {
        case 0: finish(out, kROT[y], m); break;            // documented form
        case 1: finish(out, "BIT", std::format("{}, {}", y, m)); break;
        case 2: finish(out, "RES", std::format("{}, {}", y, m)); break;
        default: finish(out, "SET", std::format("{}, {}", y, m)); break;
    }
}

// -- ED-prefixed --------------------------------------------------------------
void decode_ed(Cursor& cur, const SymbolResolver& resolve, Instruction& out) {
    const uint8_t op = cur.next();
    const uint8_t x = op >> 6, y = (op >> 3) & 7, z = op & 7;
    const uint8_t p = y >> 1, q = y & 1;

    if (x == 1) {
        switch (z) {
            case 0:
                if (y == 6) finish(out, "IN", "(C)");
                else        finish(out, "IN", std::format("{}, (C)", kR[y]));
                return;
            case 1:
                if (y == 6) finish(out, "OUT", "(C), 0");
                else        finish(out, "OUT", std::format("(C), {}", kR[y]));
                return;
            case 2:
                finish(out, q ? "ADC" : "SBC", std::format("HL, {}", kRP[p]));
                return;
            case 3:
                if (q) finish(out, "LD", std::format("{}, {}", kRP[p], mem_abs(cur.imm16(), resolve)));
                else   finish(out, "LD", std::format("{}, {}", mem_abs(cur.imm16(), resolve), kRP[p]));
                return;
            case 4: finish(out, "NEG"); return;
            case 5: finish(out, y == 1 ? "RETI" : "RETN"); return;
            case 6: finish(out, "IM", kIM[y]); return;
            default: // z == 7
                switch (y) {
                    case 0: finish(out, "LD", "I, A"); return;
                    case 1: finish(out, "LD", "R, A"); return;
                    case 2: finish(out, "LD", "A, I"); return;
                    case 3: finish(out, "LD", "A, R"); return;
                    case 4: finish(out, "RRD"); return;
                    case 5: finish(out, "RLD"); return;
                    default: finish(out, "NOP"); return;  // ED 6F-ish reserved
                }
        }
    }

    if (x == 2 && z <= 3 && y >= 4) {
        // Block instructions (LDI/CPI/.../OTDR).
        static constexpr const char* kBlock[4][4] = {
            {"LDI",  "CPI",  "INI",  "OUTI"},
            {"LDD",  "CPD",  "IND",  "OUTD"},
            {"LDIR", "CPIR", "INIR", "OTIR"},
            {"LDDR", "CPDR", "INDR", "OTDR"},
        };
        finish(out, kBlock[y - 4][z]);
        return;
    }

    finish(out, "NOP");  // all other ED opcodes are NONI/NOP on this CPU
}

// -- Unprefixed / DD / FD base table -----------------------------------------
void decode_base(uint8_t op, Cursor& cur, Index ix,
                 const SymbolResolver& resolve, Instruction& out) {
    const uint8_t x = op >> 6, y = (op >> 3) & 7, z = op & 7;
    const uint8_t p = y >> 1, q = y & 1;

    switch (x) {
        case 0:
            switch (z) {
                case 0:
                    switch (y) {
                        case 0: finish(out, "NOP"); return;
                        case 1: finish(out, "EX", "AF, AF'"); return;
                        case 2: { int8_t d = cur.disp();
                                  finish(out, "DJNZ", addr(static_cast<uint16_t>(out.address + 2 + d), resolve)); return; }
                        case 3: { int8_t d = cur.disp();
                                  finish(out, "JR", addr(static_cast<uint16_t>(out.address + 2 + d), resolve)); return; }
                        default: { int8_t d = cur.disp();
                                   finish(out, "JR", std::format("{}, {}", kCC[y - 4],
                                          addr(static_cast<uint16_t>(out.address + 2 + d), resolve))); return; }
                    }
                case 1:
                    if (q == 0) finish(out, "LD", std::format("{}, {}", rp(p, ix), hex16(cur.imm16())));
                    else        finish(out, "ADD", std::format("{}, {}", names_for(ix).full, rp(p, ix)));
                    return;
                case 2:
                    if (q == 0) {
                        switch (p) {
                            case 0: finish(out, "LD", "(BC), A"); return;
                            case 1: finish(out, "LD", "(DE), A"); return;
                            case 2: finish(out, "LD", std::format("{}, {}", mem_abs(cur.imm16(), resolve), names_for(ix).full)); return;
                            default: finish(out, "LD", std::format("{}, A", mem_abs(cur.imm16(), resolve))); return;
                        }
                    } else {
                        switch (p) {
                            case 0: finish(out, "LD", "A, (BC)"); return;
                            case 1: finish(out, "LD", "A, (DE)"); return;
                            case 2: finish(out, "LD", std::format("{}, {}", names_for(ix).full, mem_abs(cur.imm16(), resolve))); return;
                            default: finish(out, "LD", std::format("A, {}", mem_abs(cur.imm16(), resolve))); return;
                        }
                    }
                case 3:
                    finish(out, q ? "DEC" : "INC", rp(p, ix));
                    return;
                case 4: finish(out, "INC", reg(y, ix, true, cur)); return;
                case 5: finish(out, "DEC", reg(y, ix, true, cur)); return;
                case 6: { std::string d = reg(y, ix, true, cur);  // may read displacement first
                          finish(out, "LD", std::format("{}, {}", d, hex8(cur.imm8()))); return; }
                default: { // z == 7
                    static constexpr const char* kAcc[8] =
                        {"RLCA", "RRCA", "RLA", "RRA", "DAA", "CPL", "SCF", "CCF"};
                    finish(out, kAcc[y]); return;
                }
            }

        case 1:
            if (y == 6 && z == 6) { finish(out, "HALT"); return; }
            else {
                const bool has_mem = (ix != Index::None) && (y == 6 || z == 6);
                const bool allow_half = (ix != Index::None) && !has_mem;
                const std::string dst = reg(y, ix, allow_half, cur);
                const std::string src = reg(z, ix, allow_half, cur);
                finish(out, "LD", dst + ", " + src);
                return;
            }

        case 2: {
            const std::string r = reg(z, ix, true, cur);
            const std::string ops = kALU_A[y] ? ("A, " + r) : r;
            finish(out, kALU[y], ops);
            return;
        }

        default: // x == 3
            switch (z) {
                case 0: finish(out, "RET", kCC[y]); return;
                case 1:
                    if (q == 0) { finish(out, "POP", rp2(p, ix)); return; }
                    switch (p) {
                        case 0: finish(out, "RET"); return;
                        case 1: finish(out, "EXX"); return;
                        case 2: finish(out, "JP", std::format("({})", names_for(ix).full)); return;
                        default: finish(out, "LD", std::format("SP, {}", names_for(ix).full)); return;
                    }
                case 2: finish(out, "JP", std::format("{}, {}", kCC[y], addr(cur.imm16(), resolve))); return;
                case 3:
                    switch (y) {
                        case 0: finish(out, "JP", addr(cur.imm16(), resolve)); return;
                        case 1: finish(out, "NOP"); return;   // CB handled by caller
                        case 2: finish(out, "OUT", std::format("({}), A", hex8(cur.imm8()))); return;
                        case 3: finish(out, "IN", std::format("A, ({})", hex8(cur.imm8()))); return;
                        case 4: finish(out, "EX", std::format("(SP), {}", names_for(ix).full)); return;
                        case 5: finish(out, "EX", "DE, HL"); return;   // DD/FD has no effect
                        case 6: finish(out, "DI"); return;
                        default: finish(out, "EI"); return;
                    }
                case 4: finish(out, "CALL", std::format("{}, {}", kCC[y], addr(cur.imm16(), resolve))); return;
                case 5:
                    if (q == 0) { finish(out, "PUSH", rp2(p, ix)); return; }
                    if (p == 0) { finish(out, "CALL", addr(cur.imm16(), resolve)); return; }
                    finish(out, "NOP");  // DD/ED/FD handled by caller
                    return;
                case 6: {
                    const std::string r = hex8(cur.imm8());
                    const std::string ops = kALU_A[y] ? ("A, " + r) : r;
                    finish(out, kALU[y], ops);
                    return;
                }
                default: // z == 7
                    finish(out, "RST", hex8(static_cast<uint8_t>(y * 8))); return;
            }
    }
}

} // namespace

Instruction Disassembler::Decode(const ByteReader& read, uint16_t address,
                                 const SymbolResolver& resolve) const {
    Instruction out;
    out.address = address;
    Cursor cur{read, address};

    // Consume any DD/FD prefixes (last one wins); dispatch on the final opcode.
    Index ix = Index::None;
    for (;;) {
        const uint8_t op = cur.next();
        if (op == 0xDD) { ix = Index::IX; continue; }
        if (op == 0xFD) { ix = Index::IY; continue; }
        if (op == 0xED) { decode_ed(cur, resolve, out); break; }
        if (op == 0xCB) {
            if (ix == Index::None) decode_cb(cur, out);
            else                   decode_index_cb(cur, ix, out);
            break;
        }
        decode_base(op, cur, ix, resolve, out);
        break;
    }

    out.length = cur.n > 4 ? 4 : cur.n;
    out.bytes = cur.bytes;
    return out;
}

} // namespace z80::dbg
