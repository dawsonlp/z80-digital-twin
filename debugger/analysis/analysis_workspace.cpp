// Copyright (c) 2026 Larry Dawson. Licensed under the MIT License (see LICENSE).
#include "analysis_workspace.h"
#include <algorithm>
#include <set>
namespace z80::dbg::analysis {
namespace {
auto fail(ErrorCode code, std::string message) { return std::unexpected(Error{code, std::move(message)}); }
} // namespace
View::View(const Project &project) {
    std::map<uint16_t, std::vector<const SymbolRecord *>> addresses;
    std::set<std::string> reserved;
    for (const auto &[id, sym] : project.Get().symbols) {
        reserved.insert(sym.Name());
        for (const auto &[alias, p] : sym.aliases)
            reserved.insert(alias);
        if (sym.retired)
            continue;
        if (auto address = project.Address(sym)) {
            addresses[*address].push_back(&sym);
            regions_.push_back(
                {*address, sym.Name(), sym.Kind(), sym.Text(Field::Summary), sym.Extent().value_or(1)});
            names_.emplace(sym.Name(), *address);
            for (const auto &[name, p] : sym.aliases)
                names_.emplace(name, *address);
        }
    }
    for (const auto &[address, candidates] : addresses)
        if (candidates.size() == 1) {
            const auto &s = *candidates.front();
            table_.Define({address, s.Name(), s.Kind(), s.Text(Field::Summary), s.Extent().value_or(1)});
        }
    // Defaults are a transient presentation layer. A durable name, alias or
    // ambiguous address must never be shadowed by an architectural fallback.
    SymbolTable defaults;
    defaults.AddZ80VectorDefaults();
    for (const auto &symbol : defaults.List())
        if (!addresses.contains(symbol.address) && !reserved.contains(symbol.name))
            table_.Define(symbol);
}
std::optional<Symbol> View::FindContaining(uint16_t address) const {
    if (auto exact = table_.Lookup(address))
        return exact;
    std::optional<Symbol> match;
    for (const auto &region : regions_) {
        if (address < region.address || uint32_t(address - region.address) >= region.size)
            continue;
        if (match)
            return {}; // Overlapping interpretations need an explicit selection.
        match = region;
    }
    return match;
}
std::optional<uint16_t> View::Resolve(std::string_view name) const {
    if (auto found = names_.find(std::string(name)); found != names_.end())
        return found->second;
    return table_.Resolve(name);
}
Result<void> Workspace::Initialize(std::span<const uint8_t> bytes, uint16_t origin,
                                   std::span<const Symbol> seeds) {
    if (Dirty())
        return fail(ErrorCode::Conflict, "save current analysis before loading another image");
    auto image = Identify(bytes, origin);
    if (!image)
        return std::unexpected(image.error());
    auto project = Project::New(*image);
    if (!project)
        return std::unexpected(project.error());
    for (const auto &seed : seeds) {
        auto created = project->Create(project->Bind(seed.address), seed.name, seed.type, seed.size,
                                       seed.description, {Origin::Generated, Review::Accepted, {}, {}});
        if (!created)
            return std::unexpected(created.error());
    }
    View view(*project);
    std::vector<uint8_t> copy(bytes.begin(), bytes.end());
    auto revision = Revision(*project);
    auto saved = revision;
    project_ = std::move(*project);
    view_ = std::move(view);
    bytes_.swap(copy);
    revision_.swap(revision);
    saved_revision_.swap(saved);
    path_.clear();
    disk_token_.reset();
    return {};
}
Result<void> Workspace::OpenFile(const std::filesystem::path &path) {
    if (!project_)
        return fail(ErrorCode::NotFound, "load the matching image before opening analysis");
    if (Dirty())
        return fail(ErrorCode::Conflict, "save current edits before opening another analysis");
    auto loaded = Open(path, project_->Get().image);
    if (!loaded)
        return std::unexpected(loaded.error());
    View view(loaded->project);
    auto revision = Revision(loaded->project);
    auto saved = revision;
    auto canonical = std::filesystem::absolute(path).lexically_normal();
    project_ = std::move(loaded->project);
    view_ = std::move(view);
    disk_token_ = std::move(loaded->disk_token);
    path_.swap(canonical);
    revision_.swap(revision);
    saved_revision_.swap(saved);
    return {};
}
Result<void> Workspace::SaveFile(const std::filesystem::path &path) {
    if (!project_)
        return fail(ErrorCode::NotFound, "no analysis project");
    auto canonical = std::filesystem::absolute(path).lexically_normal();
    auto saved = revision_; // Allocate before publication.
    auto token = Save(*project_, canonical, canonical == path_ ? disk_token_ : std::nullopt);
    if (!token)
        return std::unexpected(token.error());
    path_.swap(canonical);
    disk_token_ = std::move(*token);
    saved_revision_.swap(saved);
    return {};
}
Result<SourceId> Workspace::ImportFile(const std::filesystem::path &path, std::string attribution) {
    auto text = ReadText(path);
    if (!text)
        return std::unexpected(text.error());
    return Apply(
        [&](Project &p) { return p.ImportLegacy(*text, path.generic_string(), std::move(attribution)); });
}
} // namespace z80::dbg::analysis
