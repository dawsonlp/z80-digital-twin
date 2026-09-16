// Copyright (c) 2026 Larry Dawson. Licensed under the MIT License (see LICENSE).
#include "analysis_export.h"
#include "content_hash.h"
#include "pasmo_source.h"
#include <algorithm>
#include <cctype>
#include <format>
#include <set>
#include <sstream>

namespace z80::dbg::analysis {
namespace {
using J = json::Value;
using O = J::Object;
using A = J::Array;
auto fail(ErrorCode code, std::string message) { return std::unexpected(Error{code, std::move(message)}); }
std::string upper(std::string_view name) {
    std::string out(name);
    for (auto &c : out)
        if (c >= 'a' && c <= 'z')
            c = static_cast<char>(c - 'a' + 'A');
    return out;
}
std::string comment(std::string_view text) {
    std::string out;
    for (unsigned char c : text) {
        if (c == '\n')
            out += "\n; ";
        else if (c < 32 || c == 127)
            out += std::format("\\x{:02x}", c);
        else
            out += static_cast<char>(c);
    }
    return out;
}
struct Definition {
    const SymbolRecord *symbol;
    std::string spelling;
    uint32_t value;
    bool image_location;
};
struct Line {
    uint32_t offset, length;
    std::string text;
    A references;
    std::optional<SymbolId> declaration;
};
} // namespace
bool PasmoIdentifier(std::string_view name) {
    auto alpha = [](char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_'; };
    if (name.empty() || !alpha(name.front()) || name.size() > 255)
        return false;
    if (!std::all_of(name.begin(), name.end(), [&](char c) { return alpha(c) || (c >= '0' && c <= '9'); }))
        return false;
    // Pasmo 0.5.5 token.cxx keyword inventory. Dot directives are excluded by
    // the lexical subset. We reject case collisions even in case-sensitive mode.
    static const std::set<std::string> reserved = [] {
        std::set<std::string> result;
        std::istringstream words(
            "A ADC ADD AF AND B BC BIT C CALL CCF CP CPD CPDR CPI CPIR CPL D DAA DB DE DEC DEFB DEFINED DEFL "
            "DEFM DEFS DEFW DI DJNZ DS DW E EI ELSE END ENDIF ENDM ENDP EQ EQU EX EXITM EXX GE GT H HALT "
            "HIGH HL I IF IFDEF IFNDEF IM IN INC INCBIN INCLUDE IND INDR INI INIR IRP IRPC IX IXH IXL IY IYH "
            "IYL JP JR L LD LDD LDDR LDI LDIR LE LOCAL LOW LT M MACRO MOD NC NE NEG NOP NOT NUL NZ OR ORG "
            "OTDR OTIR OUT OUTD OUTI P PE PO POP PROC PUBLIC PUSH R REPT RES RET RETI RETN RL RLA RLC RLCA "
            "RLD RR RRA RRC RRCA RRD RST SBC SCF SET SHL SHR SLA SLL SP SRA SRL SUB XOR Z");
        for (std::string word; words >> word;)
            result.insert(word);
        return result;
    }();
    return !reserved.contains(upper(name));
}
Result<ExportArtifacts> ExportPasmo(const Project &project, std::span<const uint8_t> bytes,
                                    const ExportOptions &options) {
    const auto &state = project.Get();
    auto identity = Identify(bytes, state.image.origin);
    if (!identity || *identity != state.image)
        return fail(ErrorCode::ImageMismatch, "export bytes do not match the project's immutable image");
    if (options.offset >= bytes.size())
        return fail(ErrorCode::Invalid, "export offset outside image");
    const auto length = options.length.value_or(static_cast<uint32_t>(bytes.size()) - options.offset);
    if (!length || length > 65535 || length > bytes.size() - options.offset)
        return fail(ErrorCode::Invalid, "Pasmo export range must contain 1..65535 image bytes");
    const auto end = options.offset + length;
    std::set<SymbolId> selected(options.selected.begin(), options.selected.end());
    for (const auto &id : selected)
        if (!state.symbols.contains(id) || state.symbols.at(id).retired)
            return fail(ErrorCode::Invalid, "selected symbol is missing or retired");
    std::map<SymbolId, Definition> definitions;
    std::map<uint32_t, SymbolId> addresses;
    std::set<std::string> names;
    A unresolved;
    for (const auto &[id, sym] : state.symbols)
        if (!sym.retired && (selected.empty() || selected.contains(id))) {
            auto spelling = sym.Text(Field::ExportName);
            if (spelling.empty())
                spelling = sym.Name();
            if (!PasmoIdentifier(spelling))
                return fail(ErrorCode::Invalid,
                            "invalid/reserved Pasmo spelling for " + sym.Name() + "; set export_name");
            if (!names.insert(upper(spelling)).second)
                return fail(ErrorCode::Conflict, "case-colliding Pasmo spelling: " + spelling);
            if (options.aliases)
                for (const auto &[alias, p] : sym.aliases) {
                    if (alias == spelling)
                        continue;
                    if (!PasmoIdentifier(alias))
                        return fail(ErrorCode::Invalid, "invalid selected alias spelling: " + alias);
                    if (!names.insert(upper(alias)).second)
                        return fail(ErrorCode::Conflict, "case-colliding alias: " + alias);
                }
            auto address = project.Address(sym);
            const auto value = address ? uint32_t(*address) : std::get<NamedValue>(sym.binding).value;
            if (value > 65535)
                return fail(ErrorCode::Unsupported, "Pasmo export supports 16-bit named values");
            if (address && !addresses.emplace(*address, id).second)
                return fail(ErrorCode::Ambiguous, "multiple selected symbols at " +
                                                      std::format("${:04x}", *address) +
                                                      "; select one interpretation");
            definitions.emplace(id, Definition{&sym, std::move(spelling), value,
                                               std::holds_alternative<ImageLocation>(sym.binding)});
        }
    struct Region {
        uint32_t begin, end;
        SymbolId id;
        std::string encoding;
    };
    std::vector<Region> regions;
    for (const auto &[id, def] : definitions) {
        const auto encoding = def.symbol->Text(Field::Encoding);
        if (encoding.empty())
            continue;
        const auto location = std::get_if<ImageLocation>(&def.symbol->binding);
        if (!location || !def.symbol->Extent())
            return fail(ErrorCode::Invalid, "data encoding needs an image binding and explicit extent");
        if (def.symbol->fields.at(Field::Encoding).provenance.review != Review::Accepted) {
            unresolved.emplace_back("Unselected data interpretation: " + id.value);
            continue;
        }
        regions.push_back({location->offset, location->offset + *def.symbol->Extent(), id, encoding});
    }
    std::sort(regions.begin(), regions.end(), [](const auto &a, const auto &b) { return a.begin < b.begin; });
    for (size_t i = 1; i < regions.size(); ++i)
        if (regions[i].begin < regions[i - 1].end)
            return fail(ErrorCode::Conflict, "selected data regions overlap");
    std::map<std::pair<uint32_t, Relation>, const Reference *> references;
    for (const auto &[id, r] : state.references)
        if (r.provenance.review == Review::Accepted)
            if (!references.emplace(std::make_pair(r.offset, r.relation), &r).second)
                return fail(ErrorCode::Conflict, "multiple selected references at an operand");
    std::set<ReferenceId> used_references;
    std::set<SymbolId> early_equates;
    std::vector<Line> lines;
    std::optional<Error> problem;
    auto resolve = [&](uint32_t offset, Relation relation, uint16_t target,
                       A &uses) -> std::optional<std::string> {
        const Definition *definition = nullptr;
        uint32_t addition = 0;
        std::optional<ReferenceId> reference_id;
        if (auto explicit_ref = references.find({offset, relation}); explicit_ref != references.end()) {
            const auto &r = *explicit_ref->second;
            auto found = definitions.find(r.target);
            if (found == definitions.end()) {
                problem = Error{ErrorCode::Invalid, "reference targets a retired or unselected symbol"};
                return {};
            }
            definition = &found->second;
            addition = r.target_offset;
            reference_id = r.id;
            if (addition > 65535 - definition->value || definition->value + addition != target) {
                problem = Error{ErrorCode::Invalid, "reference does not match encoded operand bytes"};
                return {};
            }
            used_references.insert(r.id);
        } else if (auto found = addresses.find(target); found != addresses.end())
            definition = &definitions.at(found->second);
        if (!definition)
            return {};
        uses.emplace_back(O{{"symbol", definition->symbol->id.value},
                            {"target_offset", addition},
                            {"reference", reference_id ? J(reference_id->value) : J{}},
                            {"basis", reference_id ? "selected_reference" : "decoded_address"}});
        return definition->spelling + (addition ? "+" + std::to_string(addition) : "");
    };
    for (uint32_t offset = options.offset; offset < end;) {
        const Region *region = nullptr;
        uint32_t limit = end;
        for (const auto &r : regions) {
            if (offset >= r.begin && offset < r.end) {
                region = &r;
                limit = std::min(end, r.end);
                break;
            }
            if (r.begin > offset) {
                limit = std::min(limit, r.begin);
                break;
            }
        }
        if (region) {
            uint32_t count = std::min(uint32_t{16}, limit - offset);
            std::string text;
            A uses;
            const bool word = (region->encoding == "words" || region->encoding == "pointers") &&
                              (offset - region->begin) % 2 == 0 && count >= 2;
            if (word) {
                count = 2;
                const auto target = static_cast<uint16_t>(bytes[offset] | (uint16_t(bytes[offset + 1]) << 8));
                auto name = region->encoding == "pointers" ? resolve(offset, Relation::Pointer, target, uses)
                                                           : std::nullopt;
                text = "    defw " + name.value_or(std::format("${:04x}", target));
            } else {
                if (region->encoding == "words" || region->encoding == "pointers")
                    count = 1;
                bool printable = region->encoding == "text";
                for (uint32_t i = 0; i < count; ++i)
                    printable &= bytes[offset + i] >= 32 && bytes[offset + i] < 127 &&
                                 bytes[offset + i] != '"' && bytes[offset + i] != '\\';
                if (printable)
                    text = "    defm \"" +
                           std::string(reinterpret_cast<const char *>(bytes.data() + offset), count) + "\"";
                else {
                    text = "    defb ";
                    for (uint32_t i = 0; i < count; ++i) {
                        if (i)
                            text += ", ";
                        text += std::format("${:02x}", bytes[offset + i]);
                    }
                }
            }
            lines.push_back({offset, count, std::move(text), std::move(uses), region->id});
            offset += count;
        } else {
            const auto remaining = bytes.subspan(offset, limit - offset);
            auto numeric =
                DisassemblePasmoStatement(remaining, static_cast<uint16_t>(state.image.origin + offset));
            A uses;
            SymbolResolver resolver;
            if (numeric.instruction.address_operand) {
                const auto relation = numeric.instruction.address_operand->use == AddressOperand::Use::Branch
                                          ? Relation::Branch
                                          : Relation::Memory;
                resolver = [&, offset, relation](uint16_t target) {
                    return resolve(offset, relation, target, uses);
                };
            }
            auto statement = DisassemblePasmoStatement(
                remaining, static_cast<uint16_t>(state.image.origin + offset), resolver);
            // Pasmo requires RST operands during its first pass. Their selected
            // symbol definitions must be numeric equates before any instruction.
            if (statement.instruction.mnemonic == "RST")
                for (const auto &use : uses)
                    early_equates.insert(SymbolId{use.at("symbol").string()});
            std::istringstream source(statement.source);
            uint32_t consumed = 0;
            for (std::string line; std::getline(source, line);) {
                const auto count = statement.raw_bytes
                                       ? std::min(uint32_t{16}, statement.instruction.length - consumed)
                                       : statement.instruction.length;
                lines.push_back({offset + consumed, count, std::move(line), consumed == 0 ? uses : A{}, {}});
                consumed += count;
            }
            offset += statement.instruction.length;
        }
        if (problem)
            return std::unexpected(*problem);
    }
    for (const auto &[id, r] : state.references)
        if (r.offset >= options.offset && r.offset < end && r.provenance.review == Review::Accepted &&
            !used_references.contains(id))
            return fail(ErrorCode::Invalid,
                        "selected reference does not identify a supported emitted operand: " + id.value);
    std::set<uint32_t> boundaries;
    for (const auto &line : lines)
        boundaries.insert(state.image.origin + line.offset);
    std::string assembly;
    A map;
    uint32_t line_number = 0;
    auto emit = [&](std::string text, std::optional<uint32_t> offset, uint32_t count, A symbols = {},
                    A uses = {}) {
        assembly += text + '\n';
        ++line_number;
        map.emplace_back(O{{"line", line_number},
                           {"image_offset", offset ? J(*offset) : J{}},
                           {"byte_length", count},
                           {"symbols", std::move(symbols)},
                           {"references", std::move(uses)}});
    };
    emit("; Generated from immutable image and selected analysis; edit the project, then regenerate.", {}, 0);
    emit("; Instruction decoding is tentative where no data declaration is selected.", {}, 0);
    emit(std::format("org ${:04x}", state.image.origin + options.offset), {}, 0);
    auto notes = [&](const Definition &def, std::optional<uint32_t> offset) {
        if (def.spelling != def.symbol->Name())
            emit("; display name: " + comment(def.symbol->Name()), offset, 0, A{def.symbol->id.value});
        for (auto field :
             {Field::Summary, Field::Inputs, Field::Outputs, Field::Clobbers, Field::Questions}) {
            const auto text = def.symbol->Text(field);
            if (text.empty())
                continue;
            std::istringstream comments("; " + FieldName(field) + ": " + comment(text));
            for (std::string part; std::getline(comments, part);)
                emit(part, offset, 0, A{def.symbol->id.value});
        }
    };
    std::map<uint32_t, const Definition *> labels;
    for (const auto &[id, def] : definitions) {
        if (def.image_location && boundaries.contains(def.value) && !early_equates.contains(id))
            labels.emplace(def.value, &def);
        else {
            notes(def, {});
            emit(def.spelling + " equ " + std::format("${:04x}", def.value), {}, 0, A{id.value});
        }
        if (options.aliases)
            for (const auto &[alias, p] : def.symbol->aliases)
                if (alias != def.spelling)
                    emit(alias + " equ " + def.spelling, {}, 0, A{id.value});
    }
    for (const auto &line : lines) {
        A symbols;
        if (auto label = labels.find(state.image.origin + line.offset); label != labels.end()) {
            const auto &def = *label->second;
            symbols.emplace_back(def.symbol->id.value);
            notes(def, line.offset);
            emit(def.spelling + ":", line.offset, 0, A{def.symbol->id.value});
        }
        if (line.declaration && (symbols.empty() || symbols.front().string() != line.declaration->value))
            symbols.emplace_back(line.declaration->value);
        emit(line.text, line.offset, line.length, std::move(symbols), line.references);
    }
    for (const auto &[id, proposal] : state.proposals)
        if (proposal.claim.provenance.review == Review::Proposed)
            unresolved.emplace_back("Pending proposal: " + id.value);
    const auto revision = Revision(project);
    const auto source_map = json::Write(O{{"format", "z80-analysis-source-map"},
                                          {"source", "assembly"},
                                          {"version", 1},
                                          {"image", state.image.sha256},
                                          {"project", state.id.value},
                                          {"revision", revision},
                                          {"lines", std::move(map)}});
    A selected_ids;
    for (const auto &[id, def] : definitions)
        selected_ids.emplace_back(id.value);
    auto manifest = json::Write(O{{"format", "z80-analysis-export"},
                                  {"version", 1},
                                  {"exporter", "z80-analysis-pasmo-v1"},
                                  {"dialect", "Pasmo 0.5.5"},
                                  {"project", state.id.value},
                                  {"revision", revision},
                                  {"image", state.image.sha256},
                                  {"origin", int(state.image.origin)},
                                  {"offset", options.offset},
                                  {"length", length},
                                  {"aliases", options.aliases},
                                  {"selected_symbols", std::move(selected_ids)},
                                  {"assembly_sha256", Sha256(assembly)},
                                  {"source_map_sha256", Sha256(source_map)},
                                  {"unresolved", std::move(unresolved)}});
    return ExportArtifacts{std::move(assembly), source_map, std::move(manifest)};
}
} // namespace z80::dbg::analysis
