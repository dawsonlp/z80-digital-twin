//
// Z80 Digital Twin Debugger - SymbolTable
// Copyright (c) 2025 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//
// A bidirectional map of addresses <-> labels with metadata, loaded from / saved
// to a small JSON ".sym" file. Loading is forgiving: malformed individual
// entries are skipped (optionally reported) rather than failing the whole file,
// and a missing file is a non-fatal "no symbols" result.
//
// It plugs into the disassembler via MakeResolver(), which yields a
// SymbolResolver (address -> label) for operand substitution.
//

#ifndef Z80_DBG_SYMBOL_TABLE_H
#define Z80_DBG_SYMBOL_TABLE_H

#include "disassembler.h"   // for SymbolResolver

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace z80::dbg {

/// @brief Kind of symbol, used for display and (later) coloring.
enum class SymbolType {
    Label,         ///< Generic named address.
    Function,      ///< Subroutine entry point.
    JumpTarget,    ///< Branch/loop target.
    Variable,      ///< Named data byte/word (generic).
    DataRegion,    ///< Named span of `size` bytes.
    ByteVariable,  ///< Named 8-bit data location.
    WordVariable,  ///< Named 16-bit data location.
};

/// @brief Convert a SymbolType to/from its JSON string form.
std::string ToString(SymbolType type);
std::optional<SymbolType> SymbolTypeFromString(std::string_view text);

/// @brief A single named address.
struct Symbol {
    uint16_t address = 0;
    std::string name;
    SymbolType type = SymbolType::Label;
    std::string description;
    uint16_t size = 1;   ///< Bytes covered (>=1; meaningful for DataRegion).
};

class SymbolTable {
public:
    // -- Mutation ------------------------------------------------------------

    /// @brief Add or replace the symbol at sym.address. The name index is kept
    ///        consistent (a renamed/removed address drops its old name).
    void Define(const Symbol& sym);

    /// @brief Convenience overload for a point symbol.
    void DefineLabel(uint16_t address, std::string name,
                     SymbolType type = SymbolType::Label,
                     std::string description = {});

    void Remove(uint16_t address);
    void Clear() noexcept;

    // -- Lookup --------------------------------------------------------------

    /// @brief Full symbol at an exact address.
    [[nodiscard]] std::optional<Symbol> Lookup(uint16_t address) const;

    /// @brief Label name at an exact address (for the disassembler resolver).
    [[nodiscard]] std::optional<std::string> ResolveName(uint16_t address) const;

    /// @brief Address for a label name.
    [[nodiscard]] std::optional<uint16_t> Resolve(std::string_view name) const;

    /// @brief All symbols, ordered by address.
    [[nodiscard]] std::vector<Symbol> List() const;

    [[nodiscard]] std::size_t Size() const noexcept { return by_address_.size(); }
    [[nodiscard]] bool Empty() const noexcept { return by_address_.empty(); }

    // -- Persistence ---------------------------------------------------------

    /// @brief Load symbols from a JSON .sym file, merging into this table.
    /// @param path      File to read.
    /// @param program   Optional out-param: the "program" field, if present.
    /// @param warnings  Optional out-param: per-entry/parse warnings.
    /// @return false if the file cannot be opened or its top-level JSON is
    ///         invalid; true otherwise (individual bad entries are skipped).
    bool LoadFromFile(const std::string& path,
                      std::string* program = nullptr,
                      std::vector<std::string>* warnings = nullptr);

    /// @brief Write all symbols to a JSON .sym file.
    /// @return false if the file cannot be opened for writing.
    bool SaveToFile(const std::string& path,
                    const std::string& program = {}) const;

    // -- Disassembler integration -------------------------------------------

    /// @brief A resolver bound to this table. The returned callable holds a
    ///        pointer to this table; it must not outlive the table.
    [[nodiscard]] SymbolResolver MakeResolver() const;

private:
    std::map<uint16_t, Symbol> by_address_;          ///< Ordered by address.
    std::unordered_map<std::string, uint16_t> by_name_;
};

} // namespace z80::dbg

#endif // Z80_DBG_SYMBOL_TABLE_H
