#include "pasmo_source.h"
#include "disassembler.h"

#include <algorithm>
#include <cctype>
#include <format>
#include <stdexcept>

namespace z80::dbg {
namespace {

std::string pasmo_text(std::string text) {
    for (size_t pos = 0; (pos = text.find("0x", pos)) != std::string::npos; ++pos)
        text.replace(pos, 2, "$");
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

// These are encoding-preservation exceptions, not an alternate assembler.
// Normal instruction spelling comes exclusively from Disassembler::Decode.
std::string byte_reason(std::span<const uint8_t> bytes, const Instruction& ins) {
    if (!ins.complete) return "incomplete instruction";
    if (ins.mnemonic == "JR" || ins.mnemonic == "DJNZ") {
        const int next = static_cast<int>(ins.address + ins.length);
        const int target = next + static_cast<int8_t>(bytes.back());
        if (next >= 65536 || target < 0 || target >= 65536)
            return "relative branch crosses address boundary";
    }
    size_t prefixes = 0;
    while (prefixes < bytes.size() && (bytes[prefixes] == 0xDD || bytes[prefixes] == 0xFD))
        ++prefixes;
    if (prefixes > 1) return "repeated index prefixes";
    if (prefixes == 1) {
        if (bytes[1] == 0xED) return "ignored index prefix";
        if (bytes[1] == 0xCB && (bytes[3] & 7) != 6)
            return "indexed CB encoding has no selected Pasmo spelling";
        if (ins.operands.find("IX") == std::string::npos &&
            ins.operands.find("IY") == std::string::npos)
            return "ignored index prefix";
    }
    if (bytes[0] == 0xED) {
        const uint8_t op = bytes[1];
        if (op == 0x76 || op == 0x7E)
            return "executor-specific display; retain original encoding";
        if (ins.mnemonic == "NOP") return "ED NOP encoding";
        if ((ins.mnemonic == "NEG" && op != 0x44) ||
            (ins.mnemonic == "RETN" && op != 0x45) ||
            (ins.mnemonic == "IM" && op != 0x46 && op != 0x56 && op != 0x5E) ||
            op == 0x63 || op == 0x6B)
            return "alternate encoding";
        if (op == 0x70) return "flags-only input has no selected Pasmo spelling";
        if (op == 0x71) return "zero-output instruction has no selected Pasmo spelling";
    }
    return {};
}

void append_bytes(std::string& out, std::span<const uint8_t> bytes,
                  const std::string& explanation) {
    for (size_t i = 0; i < bytes.size(); i += 16) {
        out += "    defb ";
        const size_t end = std::min(i + 16, bytes.size());
        for (size_t j = i; j < end; ++j) {
            if (j != i) out += ", ";
            out += std::format("${:02x}", bytes[j]);
        }
        if (i == 0) out += " ; " + explanation;
        out += '\n';
    }
}

} // namespace

PasmoStatement DisassemblePasmoStatement(std::span<const uint8_t> bytes, uint16_t origin,
                                         const SymbolResolver& resolve) {
    if (bytes.empty() || bytes.size() > 65536u - origin)
        throw std::invalid_argument("invalid statement range");
    const ByteReader read = [&](uint16_t address) { return bytes[address - origin]; };
    Disassembler decoder;
    auto numeric = decoder.Decode(read, origin, {}, static_cast<uint32_t>(bytes.size()));
    const auto encoded = bytes.first(numeric.length);
    const auto reason = byte_reason(encoded, numeric);
    auto ins = resolve ? decoder.Decode(read, origin, resolve, static_cast<uint32_t>(bytes.size())) : numeric;
    std::string source;
    if (!reason.empty()) {
        append_bytes(source, encoded, reason);
    } else {
        // Preserve spelling of resolved identifiers, including embedded "0x".
        // Convert only numeric tokens to Pasmo's hexadecimal spelling.
        std::string text = ins.text;
        for (size_t pos = 0; (pos = text.find("0x", pos)) != std::string::npos; ++pos) {
            if (pos == 0 || (!std::isalnum(static_cast<unsigned char>(text[pos - 1])) && text[pos - 1] != '_'))
                text.replace(pos, 2, "$");
        }
        if ((ins.mnemonic == "JR" || ins.mnemonic == "DJNZ") && ins.symbols_used.empty()) {
            const int delta = static_cast<int>(ins.length) + static_cast<int8_t>(encoded.back());
            const auto comma = text.find(',');
            text = (comma == std::string::npos ? ins.mnemonic + " " : text.substr(0, comma + 1) + " ");
            text += std::format("${:+d}", delta);
        }
        source = "    " + text + '\n';
    }
    return {std::move(ins), std::move(source), !reason.empty()};
}

std::string DisassemblePasmo(std::span<const uint8_t> bytes, uint16_t origin) {
    if (bytes.empty() || bytes.size() == 65536)
        throw std::invalid_argument("Pasmo 0.5.5 round-trip range must contain 1..65535 bytes");
    if (bytes.size() > 65536u - origin)
        throw std::invalid_argument("binary range exceeds the 64 KB address space");
    std::string out = "; Linear disassembly; byte directives preserve exceptional encodings.\n";
    out += std::format("org ${:04x}\n", origin);
    Disassembler decoder;
    const ByteReader read = [&](uint16_t address) { return bytes[address - origin]; };
    for (size_t offset = 0; offset < bytes.size();) {
        const auto ins = decoder.Decode(read, static_cast<uint16_t>(origin + offset), {},
                                        static_cast<uint32_t>(bytes.size() - offset));
        const auto encoded = bytes.subspan(offset, ins.length);
        const std::string reason = byte_reason(encoded, ins);
        if (!reason.empty()) {
            append_bytes(out, encoded, reason +
                         (ins.complete ? "; display: " + pasmo_text(ins.text) : ""));
        } else {
            std::string text = pasmo_text(ins.text);
            // Express relative branches relative to this source location.
            // Pasmo rejects some wrapping targets; those retain raw bytes above.
            if (ins.mnemonic == "JR" || ins.mnemonic == "DJNZ") {
                const int delta = static_cast<int>(ins.length) + static_cast<int8_t>(encoded.back());
                const auto comma = text.find(',');
                text = (comma == std::string::npos ? pasmo_text(ins.mnemonic) + " "
                                                  : text.substr(0, comma + 1) + " ");
                text += std::format("${:+d}", delta);
            }
            out += "    " + text + '\n';
        }
        offset += ins.length;
    }
    return out;
}

} // namespace z80::dbg
