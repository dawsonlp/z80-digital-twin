// Copyright (c) 2026 Larry Dawson. Licensed under the MIT License (see LICENSE).
#include "analysis_project.h"
#include "analysis_json.h"
#include "content_hash.h"
#include <algorithm>
#include <array>
#include <charconv>
#include <format>
#include <random>
#include <set>

namespace z80::dbg::analysis {
namespace {
auto fail(ErrorCode code, std::string message) { return std::unexpected(Error{code, std::move(message)}); }
bool hex(std::string_view s, size_t length) {
    return s.size() == length && s.find_first_not_of("0123456789abcdef") == s.npos;
}
bool id(std::string_view s, std::string_view prefix) {
    return s.starts_with(prefix) && hex(s.substr(prefix.size()), 32);
}
bool text_ok(std::string_view text) {
    try {
        (void)json::Write(std::string(text));
        return true;
    } catch (const std::invalid_argument &) {
        return false;
    }
}
bool name_ok(std::string_view name) {
    return !name.empty() && name.size() <= 4096 && text_ok(name) &&
           std::none_of(name.begin(), name.end(), [](unsigned char c) { return c < 32 || c == 127; });
}
Result<void> provenance_ok(const State &s, const Provenance &p) {
    if (p.origin < Origin::User || p.origin > Origin::Generated || p.review < Review::Proposed ||
        p.review > Review::Superseded)
        return fail(ErrorCode::Invalid, "invalid provenance enum");
    if (p.source && !s.sources.contains(*p.source))
        return fail(ErrorCode::Invalid, "dangling source reference");
    for (const auto &e : p.evidence)
        if (!s.evidence.contains(e))
            return fail(ErrorCode::Invalid, "dangling evidence reference");
    return {};
}
Result<void> field_ok(Field field, const FieldValue &value) {
    if (field == Field::Kind) {
        auto p = std::get_if<SymbolType>(&value);
        if (!p || *p < SymbolType::Label || *p > SymbolType::WordVariable)
            return fail(ErrorCode::Invalid, "invalid symbol kind");
    } else if (field == Field::Extent) {
        auto p = std::get_if<std::optional<uint32_t>>(&value);
        if (!p || (*p && (**p == 0 || **p > 65536)))
            return fail(ErrorCode::Invalid, "extent must be unknown or 1..65536");
    } else {
        auto p = std::get_if<std::string>(&value);
        if (!p || field < Field::Name || field > Field::Encoding || !text_ok(*p))
            return fail(ErrorCode::Invalid, "invalid text field");
        if ((field == Field::Name || field == Field::ExportName) && !name_ok(*p))
            return fail(ErrorCode::Invalid, "invalid or empty name");
        if (field == Field::Encoding && *p != "bytes" && *p != "words" && *p != "text" && *p != "pointers")
            return fail(ErrorCode::Invalid, "unknown data encoding");
    }
    return {};
}
Result<void> edit(State &state, SymbolId id, const std::map<Field, FieldValue> &changes,
                  const Provenance &p) {
    auto it = state.symbols.find(id);
    if (it == state.symbols.end() || it->second.retired)
        return fail(ErrorCode::NotFound, "symbol is missing or retired");
    auto &sym = it->second;
    for (const auto &[field, value] : changes) {
        if (auto valid = field_ok(field, value); !valid)
            return valid;
        auto old = sym.fields.find(field);
        if (old != sym.fields.end() && old->second.value == value)
            continue;
        if (field == Field::Name) {
            sym.historical_names.push_back(sym.fields.at(Field::Name));
            sym.aliases.emplace(sym.Name(), sym.fields.at(Field::Name).provenance);
            sym.aliases.erase(std::get<std::string>(value));
        }
        for (auto &[key, proposal] : state.proposals)
            if (proposal.symbol == id && proposal.field == field &&
                proposal.claim.provenance.review == Review::Accepted)
                proposal.claim.provenance.review = Review::Superseded;
        sym.fields.insert_or_assign(field, Claim{value, p});
    }
    return {};
}
} // namespace
std::string NewId(std::string_view prefix) {
    std::random_device random;
    std::string out(prefix);
    for (int i = 0; i < 4; ++i)
        out += std::format("{:08x}", static_cast<uint32_t>(random()));
    return out;
}
const std::string &SymbolRecord::Name() const { return std::get<std::string>(fields.at(Field::Name).value); }
SymbolType SymbolRecord::Kind() const { return std::get<SymbolType>(fields.at(Field::Kind).value); }
std::optional<uint32_t> SymbolRecord::Extent() const {
    return std::get<std::optional<uint32_t>>(fields.at(Field::Extent).value);
}
std::string SymbolRecord::Text(Field field) const {
    auto it = fields.find(field);
    return it == fields.end() ? std::string{} : std::get<std::string>(it->second.value);
}
std::string FieldName(Field field) {
    constexpr std::array names = {"name",    "kind",     "extent",    "summary",     "inputs",
                                  "outputs", "clobbers", "questions", "export_name", "encoding"};
    return names.at(static_cast<size_t>(field));
}
Result<Field> ParseField(std::string_view text) {
    for (int i = 0; i <= static_cast<int>(Field::Encoding); ++i)
        if (FieldName(static_cast<Field>(i)) == text)
            return static_cast<Field>(i);
    return fail(ErrorCode::Invalid, "unknown symbol field: " + std::string(text));
}
Result<Image> Identify(std::span<const uint8_t> bytes, uint16_t origin) {
    if (bytes.empty() || bytes.size() > 65536u - origin)
        return fail(ErrorCode::Invalid, "image must fit the non-wrapping 16-bit address space");
    return Image{Sha256(bytes), static_cast<uint32_t>(bytes.size()), origin};
}
Result<Project> Project::New(const Image &image) {
    State state;
    state.id.value = NewId("prj_");
    state.image = image;
    return Restore(std::move(state));
}
Result<Project> Project::Restore(State state) {
    if (auto valid = Validate(state); !valid)
        return std::unexpected(valid.error());
    return Project(std::move(state));
}
Result<void> Project::Commit(State state) {
    if (auto valid = Validate(state); !valid)
        return valid;
    static_assert(std::is_nothrow_swappable_v<State>);
    std::swap(state_, state);
    return {};
}
Binding Project::Bind(uint16_t address) const {
    const auto &image = state_.image;
    if (address >= image.origin && address - image.origin < image.size)
        return ImageLocation{image.sha256, static_cast<uint32_t>(address - image.origin)};
    return ExternalLocation{state_.address_space, address};
}
std::optional<uint16_t> Project::Address(const SymbolRecord &sym) const {
    if (const auto p = std::get_if<ImageLocation>(&sym.binding))
        return static_cast<uint16_t>(state_.image.origin + p->offset);
    if (const auto p = std::get_if<ExternalLocation>(&sym.binding))
        return p->address;
    return {};
}
Result<SymbolId> Project::Create(Binding binding, std::string name, SymbolType kind,
                                 std::optional<uint32_t> extent, std::string summary, Provenance provenance) {
    State next = state_;
    SymbolRecord sym;
    sym.id.value = NewId("sym_");
    sym.binding = std::move(binding);
    sym.fields = {{Field::Name, {std::move(name), provenance}},
                  {Field::Kind, {kind, provenance}},
                  {Field::Extent, {extent, provenance}},
                  {Field::Summary, {std::move(summary), provenance}}};
    auto key = sym.id;
    if (!next.symbols.emplace(key, std::move(sym)).second)
        return fail(ErrorCode::Conflict, "generated ID collision");
    if (auto result = Commit(std::move(next)); !result)
        return std::unexpected(result.error());
    return key;
}
Result<void> Project::Edit(SymbolId id, const std::map<Field, FieldValue> &changes, Provenance p) {
    State next = state_;
    if (auto result = edit(next, id, changes, p); !result)
        return result;
    return Commit(std::move(next));
}
Result<void> Project::Alias(SymbolId id, std::string name, bool add, Provenance p) {
    State next = state_;
    auto it = next.symbols.find(id);
    if (it == next.symbols.end() || it->second.retired)
        return fail(ErrorCode::NotFound, "symbol is missing or retired");
    if (name == it->second.Name())
        return fail(ErrorCode::Conflict, "preferred name cannot also be an alias");
    if (add)
        it->second.aliases.emplace(std::move(name), std::move(p));
    else if (auto alias = it->second.aliases.find(name); alias != it->second.aliases.end()) {
        it->second.historical_names.push_back({alias->first, alias->second});
        it->second.aliases.erase(alias);
    }
    return Commit(std::move(next));
}
Result<void> Project::Retire(SymbolId id) {
    State next = state_;
    auto it = next.symbols.find(id);
    if (it == next.symbols.end())
        return fail(ErrorCode::NotFound, "symbol not found");
    it->second.retired = true;
    return Commit(std::move(next));
}
Result<SymbolId> Project::Resolve(std::string_view name) const {
    std::optional<SymbolId> found;
    for (const auto &[id, sym] : state_.symbols)
        if (!sym.retired && (sym.Name() == name || sym.aliases.contains(std::string(name)))) {
            if (found)
                return fail(ErrorCode::Ambiguous, "ambiguous symbol name");
            found = id;
        }
    if (!found)
        return fail(ErrorCode::NotFound, "symbol name not found");
    return *found;
}
std::vector<SymbolId> Project::At(uint16_t address) const {
    std::vector<SymbolId> result;
    for (const auto &[id, sym] : state_.symbols)
        if (!sym.retired && Address(sym) == address)
            result.push_back(id);
    return result;
}
Result<void> Project::Validate(const State &s) {
    if (!id(s.id.value, "prj_") || s.target != "z80" || s.address_space != "z80:flat16")
        return fail(ErrorCode::Unsupported, "unsupported project identity or target");
    if (!hex(s.image.sha256, 64) || s.image.size == 0 || s.image.size > 65536u - s.image.origin)
        return fail(ErrorCode::Invalid, "invalid image identity/range");
    std::set<std::string> names, source_keys, proposal_keys;
    for (const auto &[key, source] : s.sources) {
        if (key != source.id || !id(key.value, "src_") || !hex(source.sha256, 64) || source.key.empty() ||
            !text_ok(source.key) || !text_ok(source.attribution) || !source_keys.insert(source.key).second)
            return fail(ErrorCode::Invalid, "invalid or duplicate import source");
    }
    for (const auto &[key, evidence] : s.evidence) {
        if (key != evidence.id || !id(key.value, "ev_") || evidence.image != s.image.sha256 ||
            evidence.source_key.empty() || evidence.content.empty() || !text_ok(evidence.content) ||
            !text_ok(evidence.source_key) || !text_ok(evidence.limitations))
            return fail(ErrorCode::Invalid, "invalid evidence identity or content");
        if (evidence.offset &&
            (*evidence.offset >= s.image.size || evidence.bytes.size() > s.image.size - *evidence.offset))
            return fail(ErrorCode::Invalid, "evidence outside image");
        if (!evidence.offset && !evidence.bytes.empty())
            return fail(ErrorCode::Invalid, "evidence bytes need an image offset");
    }
    for (const auto &[key, sym] : s.symbols) {
        if (key != sym.id || !id(key.value, "sym_"))
            return fail(ErrorCode::Invalid, "invalid symbol ID");
        for (auto field : {Field::Name, Field::Kind, Field::Extent, Field::Summary})
            if (!sym.fields.contains(field))
                return fail(ErrorCode::Invalid, "missing symbol field");
        for (const auto &[field, claim] : sym.fields) {
            if (auto result = field_ok(field, claim.value); !result)
                return result;
            if (auto result = provenance_ok(s, claim.provenance); !result)
                return result;
        }
        if (!names.insert(sym.Name()).second)
            return fail(ErrorCode::Conflict, "name already belongs to another symbol: " + sym.Name());
        for (const auto &[alias, p] : sym.aliases) {
            if (!name_ok(alias) || !names.insert(alias).second)
                return fail(ErrorCode::Conflict, "invalid or conflicting alias: " + alias);
            if (auto result = provenance_ok(s, p); !result)
                return result;
        }
        for (const auto &historical : sym.historical_names) {
            if (auto result = field_ok(Field::Name, historical.value); !result)
                return result;
            if (auto result = provenance_ok(s, historical.provenance); !result)
                return result;
        }
        const auto extent = sym.Extent().value_or(1);
        if (const auto p = std::get_if<ImageLocation>(&sym.binding)) {
            if (p->image != s.image.sha256 || p->offset >= s.image.size || extent > s.image.size - p->offset)
                return fail(ErrorCode::Invalid, "symbol extent outside bound image");
        } else if (const auto p = std::get_if<ExternalLocation>(&sym.binding)) {
            if (p->address_space != s.address_space || extent > 65536u - p->address)
                return fail(ErrorCode::Invalid, "external symbol extent outside address space");
        } else {
            const auto value = std::get<NamedValue>(sym.binding);
            if ((value.width != 8 && value.width != 16 && value.width != 32) ||
                (value.width < 32 && value.value >= (uint32_t{1} << value.width)) || sym.Extent())
                return fail(ErrorCode::Invalid, "invalid named value width/extent");
        }
    }
    std::set<std::pair<uint32_t, Relation>> reference_sites;
    for (const auto &[key, ref] : s.references) {
        if (key != ref.id || !id(key.value, "ref_") || ref.offset >= s.image.size ||
            !s.symbols.contains(ref.target) || ref.relation < Relation::Branch ||
            ref.relation > Relation::Pointer)
            return fail(ErrorCode::Invalid, "invalid or dangling reference");
        if (!reference_sites.emplace(ref.offset, ref.relation).second)
            return fail(ErrorCode::Conflict, "duplicate reference at operand");
        const auto &target = s.symbols.at(ref.target);
        if (const auto location = std::get_if<ImageLocation>(&target.binding);
            location && ref.target_offset >= s.image.size - location->offset)
            return fail(ErrorCode::Invalid, "reference offset outside image");
        if (const auto location = std::get_if<ExternalLocation>(&target.binding);
            location && ref.target_offset > 65535u - location->address)
            return fail(ErrorCode::Invalid, "reference offset outside address space");
        if (target.Extent() && ref.target_offset >= *target.Extent())
            return fail(ErrorCode::Invalid, "reference offset exceeds target extent");
        if (ref.target_offset > 65535)
            return fail(ErrorCode::Invalid, "reference offset out of range");
        if (auto result = provenance_ok(s, ref.provenance); !result)
            return result;
    }
    for (const auto &[key, prop] : s.proposals) {
        if (key != prop.id || !id(key.value, "prop_") || !s.symbols.contains(prop.symbol) ||
            prop.key.empty() || !text_ok(prop.key) || !proposal_keys.insert(prop.key).second)
            return fail(ErrorCode::Invalid, "invalid or dangling proposal");
        if (auto result = field_ok(prop.field, prop.claim.value); !result)
            return result;
        if (auto result = provenance_ok(s, prop.claim.provenance); !result)
            return result;
    }
    return {};
}
Result<EvidenceId> Project::Attach(Evidence evidence) {
    State next = state_;
    evidence.id.value = NewId("ev_");
    auto key = evidence.id;
    if (!next.evidence.emplace(key, std::move(evidence)).second)
        return fail(ErrorCode::Conflict, "generated evidence ID collision");
    if (auto result = Commit(std::move(next)); !result)
        return std::unexpected(result.error());
    return key;
}
Result<ReferenceId> Project::Refer(Reference reference) {
    State next = state_;
    for (const auto &[id, prior] : next.references)
        if (prior.offset == reference.offset && prior.relation == reference.relation)
            return fail(ErrorCode::Conflict, "reference already selected at this operand");
    reference.id.value = NewId("ref_");
    auto key = reference.id;
    if (!next.references.emplace(key, std::move(reference)).second)
        return fail(ErrorCode::Conflict, "generated reference ID collision");
    if (auto result = Commit(std::move(next)); !result)
        return std::unexpected(result.error());
    return key;
}
Result<ProposalId> Project::Propose(Proposal proposal) {
    for (const auto &[key, prior] : state_.proposals)
        if (prior.key == proposal.key) {
            if (prior.symbol == proposal.symbol && prior.field == proposal.field &&
                prior.claim.value == proposal.claim.value &&
                prior.claim.provenance.origin == proposal.claim.provenance.origin &&
                prior.claim.provenance.source == proposal.claim.provenance.source &&
                prior.claim.provenance.evidence == proposal.claim.provenance.evidence)
                return key;
            return fail(ErrorCode::Conflict, "proposal key reused with different content");
        }
    State next = state_;
    proposal.id.value = NewId("prop_");
    proposal.claim.provenance.review = Review::Proposed;
    auto key = proposal.id;
    if (!next.proposals.emplace(key, std::move(proposal)).second)
        return fail(ErrorCode::Conflict, "generated proposal ID collision");
    if (auto result = Commit(std::move(next)); !result)
        return std::unexpected(result.error());
    return key;
}
Result<void> Project::ReviewProposal(ProposalId key, Review review) {
    State next = state_;
    auto it = next.proposals.find(key);
    if (it == next.proposals.end())
        return fail(ErrorCode::NotFound, "proposal not found");
    if (review != Review::Accepted && review != Review::Rejected)
        return fail(ErrorCode::Invalid, "choose accepted or rejected");
    auto &p = it->second;
    if (p.claim.provenance.review == review)
        return {};
    if (p.claim.provenance.review != Review::Proposed)
        return fail(ErrorCode::Conflict, "proposal was already resolved");
    if (review == Review::Accepted) {
        auto provenance = p.claim.provenance;
        provenance.review = Review::Accepted;
        if (auto result = edit(next, p.symbol, {{p.field, p.claim.value}}, provenance); !result)
            return result;
        next.symbols.at(p.symbol).fields.at(p.field).provenance = std::move(provenance);
    }
    p.claim.provenance.review = review;
    return Commit(std::move(next));
}

} // namespace z80::dbg::analysis
