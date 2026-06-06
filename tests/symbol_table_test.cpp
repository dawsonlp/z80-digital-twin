//
// Z80 Digital Twin Debugger - SymbolTable tests
// Copyright (c) 2025 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//

#include "symbol_table.h"
#include "disassembler.h"

#include <array>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

using namespace z80::dbg;

int failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << (ok ? "  ✓ " : "  ✗ ") << what << '\n';
    if (!ok) ++failures;
}

void write_file(const std::string& path, const std::string& content) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f << content;
}

} // namespace

int main() {
    std::cout << "SymbolTable tests\n=================\n";

    // --- Basic define / lookup / resolve ------------------------------------
    std::cout << "\n[1] Define / lookup / resolve\n";
    {
        SymbolTable t;
        t.DefineLabel(0x0000, "MAIN", SymbolType::Function, "Entry point");
        t.DefineLabel(0x2000, "LOOP", SymbolType::JumpTarget);
        t.Define(Symbol{0x0100, "SCREEN", SymbolType::DataRegion, "framebuffer", 6144});

        check(t.Size() == 3, "size = 3");
        auto main = t.Lookup(0x0000);
        check(main && main->name == "MAIN" && main->type == SymbolType::Function,
              "lookup 0x0000 -> MAIN/Function");
        check(t.Resolve("LOOP") == std::optional<uint16_t>(0x2000), "resolve LOOP -> 0x2000");
        check(t.ResolveName(0x0100) == std::optional<std::string>("SCREEN"),
              "resolveName 0x0100 -> SCREEN");
        check(!t.Lookup(0x1234).has_value(), "lookup unknown -> none");

        auto list = t.List();
        check(list.size() == 3 && list[0].address == 0x0000 &&
              list[1].address == 0x0100 && list[2].address == 0x2000,
              "List() ordered by address");
    }

    // --- Name-index consistency on redefine ---------------------------------
    std::cout << "\n[2] Redefining an address updates the name index\n";
    {
        SymbolTable t;
        t.DefineLabel(0x0050, "OLD");
        t.DefineLabel(0x0050, "NEW");
        check(!t.Resolve("OLD").has_value(), "old name no longer resolves");
        check(t.Resolve("NEW") == std::optional<uint16_t>(0x0050), "new name resolves");
        t.Remove(0x0050);
        check(t.Empty(), "removed -> empty");
        check(!t.Resolve("NEW").has_value(), "removed name no longer resolves");
    }

    // --- Save / load round-trip ---------------------------------------------
    std::cout << "\n[3] Save / load round-trip\n";
    {
        SymbolTable t;
        t.DefineLabel(0x0000, "MAIN", SymbolType::Function, "Entry \"point\"\twith escapes");
        t.Define(Symbol{0x0100, "SCREEN_BUFFER", SymbolType::DataRegion, "320x200", 6144});
        t.DefineLabel(0x2000, "LOOP", SymbolType::JumpTarget);

        const std::string path = "/tmp/z80_symbols_roundtrip.sym";
        check(t.SaveToFile(path, "program.bin"), "SaveToFile ok");

        SymbolTable loaded;
        std::string program;
        std::vector<std::string> warnings;
        check(loaded.LoadFromFile(path, &program, &warnings), "LoadFromFile ok");
        check(warnings.empty(), "no warnings on clean file");
        check(program == "program.bin", "program field round-tripped");
        check(loaded.Size() == 3, "all 3 symbols loaded");

        auto s = loaded.Lookup(0x0100);
        check(s && s->type == SymbolType::DataRegion && s->size == 6144,
              "DataRegion size round-tripped");
        auto m = loaded.Lookup(0x0000);
        check(m && m->description == "Entry \"point\"\twith escapes",
              "escaped description round-tripped");
    }

    // --- Forgiving load: bad entries skipped, file still loads --------------
    std::cout << "\n[4] Malformed entries are skipped (non-fatal)\n";
    {
        const std::string path = "/tmp/z80_symbols_messy.sym";
        write_file(path, R"({
          "version": "1.0",
          "symbols": [
            { "address": "0x0000", "name": "GOOD", "type": "FUNCTION" },
            { "address": "0x0010" },
            { "name": "NOADDR" },
            { "address": "0x0020", "name": "DECADDR2", "type": "WAT" },
            { "address": 48, "name": "NUMERIC" }
          ]
        })");

        SymbolTable t;
        std::vector<std::string> warnings;
        check(t.LoadFromFile(path, nullptr, &warnings), "messy file still loads");
        check(t.Resolve("GOOD") == std::optional<uint16_t>(0x0000), "good entry loaded");
        check(t.Resolve("NUMERIC") == std::optional<uint16_t>(48), "numeric address loaded");
        check(t.Lookup(0x0020).has_value() &&
              t.Lookup(0x0020)->type == SymbolType::Label,
              "unknown type defaulted to LABEL");
        check(!t.Resolve("NOADDR").has_value(), "entry without address skipped");
        check(t.Size() == 3, "exactly the 3 valid entries kept");
        check(!warnings.empty(), "warnings recorded for skipped entries");
    }

    // --- Hard failures: missing file, invalid JSON --------------------------
    std::cout << "\n[5] Missing file / invalid JSON are non-fatal failures\n";
    {
        SymbolTable t;
        check(!t.LoadFromFile("/tmp/does_not_exist_z80.sym"), "missing file -> false");

        const std::string bad = "/tmp/z80_symbols_bad.sym";
        write_file(bad, "{ this is not json ]");
        std::vector<std::string> warnings;
        check(!t.LoadFromFile(bad, nullptr, &warnings), "invalid JSON -> false");
        check(!warnings.empty(), "parse error reported");
    }

    // --- FindContaining: range lookup (powers memory hover tooltips) --------
    std::cout << "\n[6b] FindContaining range lookup\n";
    {
        SymbolTable t;
        t.Define(Symbol{0x5C00, "KSTATE", SymbolType::DataRegion, "keyboard", 8});
        t.DefineLabel(0x5C08, "LAST_K", SymbolType::ByteVariable);

        auto exact = t.FindContaining(0x5C00);
        check(exact && exact->name == "KSTATE", "exact start -> KSTATE");
        auto mid = t.FindContaining(0x5C03);
        check(mid && mid->name == "KSTATE", "mid-region 0x5C03 -> KSTATE");
        auto last = t.FindContaining(0x5C07);
        check(last && last->name == "KSTATE", "region end 0x5C07 -> KSTATE");
        auto next = t.FindContaining(0x5C08);
        check(next && next->name == "LAST_K", "0x5C08 -> LAST_K (not KSTATE)");
        check(!t.FindContaining(0x5C09).has_value(), "0x5C09 -> none (past both)");
        check(!t.FindContaining(0x4000).has_value(), "below first symbol -> none");
    }

    // --- Disassembler integration via MakeResolver --------------------------
    std::cout << "\n[6] Disassembler uses the symbol table\n";
    {
        SymbolTable t;
        t.DefineLabel(0x1234, "MAIN", SymbolType::Function);

        std::array<uint8_t, 65536> mem{};
        mem[0] = 0xC3; mem[1] = 0x34; mem[2] = 0x12;   // JP 0x1234
        ByteReader read = [&](uint16_t a) { return mem[a]; };

        Disassembler d;
        Instruction with = d.Decode(read, 0x0000, t.MakeResolver());
        check(with.text == "JP MAIN", "JP resolves to label");

        Instruction without = d.Decode(read, 0x0000);
        check(without.text == "JP 0x1234", "no resolver -> hex");
    }

    std::cout << "\n=================\n";
    if (failures == 0) {
        std::cout << "✅ ALL SYMBOL-TABLE CHECKS PASSED\n";
        return 0;
    }
    std::cout << "❌ " << failures << " check(s) FAILED\n";
    return 1;
}
