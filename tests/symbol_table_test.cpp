//
// Z80 Digital Twin Debugger - SymbolTable tests
// Copyright (c) 2025-2026 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//

#include "symbol_table.h"
#include "disassembler.h"

#include <array>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <stdexcept>
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

    {
        SymbolTable defaults;
        defaults.AddZ80VectorDefaults();
        check(defaults.Size() == 9, "eight restart entries plus NMI");
        check(defaults.Resolve("RST_38_IM1") == 0x38 && defaults.Resolve("NMI_66") == 0x66,
              "interrupt entries are named and navigable");
        defaults.DefineLabel(0x38, "USER_IRQ");
        defaults.AddZ80VectorDefaults();
        check(defaults.ResolveName(0x38) == "USER_IRQ", "user vector label survives default seeding");
        defaults.Clear(); defaults.DefineLabel(0x9000, "RST_08");
        defaults.AddZ80VectorDefaults();
        check(defaults.Resolve("RST_08") == 0x9000 && !defaults.Lookup(8),
              "default names do not steal user names");
    }

    {
        SymbolTable table;
        table.Define({0x4000, "SCREEN", SymbolType::DataRegion, "Display bytes", 6912});
        table.DefineLabel(0x8000, "ENTRY");
        table.Rename(0x4000, "DISPLAY");
        const auto region = table.Lookup(0x4000);
        check(region && region->description == "Display bytes" && region->size == 6912 &&
              region->type == SymbolType::DataRegion, "rename preserves region metadata");
        check(!table.Resolve("SCREEN") && table.Resolve("DISPLAY") == 0x4000,
              "rename updates both indexes");
        for (bool replace : {false, true}) {
            bool rejected = false;
            try {
                if (replace) table.Define({0x4000, "ENTRY", SymbolType::Label, "lost", 1});
                else table.Rename(0x4000, "ENTRY");
            } catch (const std::invalid_argument&) { rejected = true; }
            check(rejected && table.Resolve("ENTRY") == 0x8000 &&
                  table.Resolve("DISPLAY") == 0x4000 && table.Lookup(0x4000)->size == 6912,
                  "conflicting rename/replacement leaves both symbols unchanged");
        }
        bool rejected = false;
        try { table.DefineLabel(0x9000, "DISPLAY"); }
        catch (const std::invalid_argument&) { rejected = true; }
        check(rejected && !table.Lookup(0x9000), "new address cannot implicitly move an existing name");
        rejected = false;
        try { table.Rename(0x9999, "MISSING"); }
        catch (const std::out_of_range&) { rejected = true; }
        check(rejected && table.Size() == 2, "rename of missing symbol does not create one");
        rejected = false;
        try { table.Rename(0x4000, ""); }
        catch (const std::invalid_argument&) { rejected = true; }
        check(rejected && table.Resolve("DISPLAY") == 0x4000, "empty rename preserves old symbol");
        table.Rename(0x4000, "DISPLAY");
        table.Remove(0x8000);
        check(table.Resolve("DISPLAY") == 0x4000 && !table.Resolve("ENTRY"),
              "same-name rename and removal preserve index consistency");

        const std::string path = "/tmp/z80_symbols_collisions.sym";
        write_file(path, R"({"symbols":[
            {"address":"0x9000","name":"DISPLAY"},
            {"address":"0x9100","name":"NEW"}]})");
        std::vector<std::string> warnings;
        check(table.LoadFromFile(path, nullptr, &warnings) && warnings.size() == 1 &&
              table.Resolve("DISPLAY") == 0x4000 && !table.Lookup(0x9000) &&
              table.Resolve("NEW") == 0x9100, "legacy import skips collisions with a warning");
        std::remove(path.c_str());
    }

    std::cout << "\n=================\n";
    if (failures == 0) {
        std::cout << "✅ ALL SYMBOL-TABLE CHECKS PASSED\n";
        return 0;
    }
    std::cout << "❌ " << failures << " check(s) FAILED\n";
    return 1;
}
