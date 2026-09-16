// Copyright (c) 2026 Larry Dawson. Licensed under the MIT License (see LICENSE).
#pragma once
#include "analysis_storage.h"
#include <utility>

namespace z80::dbg::analysis {
// Rebuilt read view; never an independently editable symbol store.
class View {
  public:
    View() { table_.AddZ80VectorDefaults(); }
    explicit View(const Project &project);
    std::optional<Symbol> Lookup(uint16_t a) const { return table_.Lookup(a); }
    std::optional<Symbol> FindContaining(uint16_t address) const;
    std::optional<std::string> ResolveName(uint16_t a) const { return table_.ResolveName(a); }
    std::optional<uint16_t> Resolve(std::string_view name) const;
    SymbolResolver MakeResolver() const { return table_.MakeResolver(); }
    size_t Size() const noexcept { return table_.Size(); }

  private:
    SymbolTable table_;
    std::vector<Symbol> regions_;
    std::map<std::string, uint16_t> names_;
};
class Workspace {
  public:
    [[nodiscard]] Result<void> Initialize(std::span<const uint8_t> bytes, uint16_t origin,
                                          std::span<const Symbol> seeds = {});
    const std::optional<Project> &Active() const noexcept { return project_; }
    const View &Symbols() const noexcept { return view_; }
    std::span<const uint8_t> Bytes() const noexcept { return bytes_; }
    bool Dirty() const noexcept { return revision_ != saved_revision_; }
    const std::filesystem::path &Path() const noexcept { return path_; }
    const std::string &CurrentRevision() const noexcept { return revision_; }
    [[nodiscard]] Result<void> OpenFile(const std::filesystem::path &path);
    [[nodiscard]] Result<void> SaveFile(const std::filesystem::path &path);
    [[nodiscard]] Result<SourceId> ImportFile(const std::filesystem::path &path,
                                              std::string attribution = "unknown");
    // Stage every operation and its derived view before publishing either.
    template <class Function>
    auto Apply(Function &&operation) -> decltype(operation(std::declval<Project &>())) {
        using R = decltype(operation(std::declval<Project &>()));
        if (!project_)
            return R(std::unexpected(Error{ErrorCode::NotFound, "load an image before editing analysis"}));
        auto next = *project_;
        auto result = operation(next);
        if (!result)
            return result;
        View view(next);
        auto revision = Revision(next);
        std::swap(*project_, next);
        std::swap(view_, view);
        revision_.swap(revision);
        return result;
    }

  private:
    std::optional<Project> project_;
    View view_;
    std::vector<uint8_t> bytes_;
    std::filesystem::path path_;
    std::optional<std::string> disk_token_;
    std::string revision_, saved_revision_;
};
} // namespace z80::dbg::analysis
