// Copyright (c) 2026 Larry Dawson. Licensed under the MIT License (see LICENSE).
// Durable symbol identity and interpretation. UI-free; no CPU ownership.
#pragma once
#include "symbol_table.h"
#include <compare>
#include <expected>
#include <map>
#include <optional>
#include <span>
#include <variant>

namespace z80::dbg::analysis {
template <class Tag> struct Id {
    std::string value;
    auto operator<=>(const Id &) const = default;
};
using ProjectId = Id<struct ProjectTag>;
using SymbolId = Id<struct SymbolTag>;
using SourceId = Id<struct SourceTag>;
using EvidenceId = Id<struct EvidenceTag>;
using ReferenceId = Id<struct ReferenceTag>;
using ProposalId = Id<struct ProposalTag>;

enum class ErrorCode { Invalid, NotFound, Conflict, Ambiguous, ImageMismatch, Io, Unsupported };
struct Error {
    ErrorCode code;
    std::string message;
};
template <class T> using Result = std::expected<T, Error>;

enum class Origin { User, Imported, Static, Runtime, Generated };
enum class Review { Proposed, Accepted, Rejected, Superseded };
struct Provenance {
    Origin origin = Origin::User;
    Review review = Review::Accepted;
    std::optional<SourceId> source;
    std::vector<EvidenceId> evidence;
    bool operator==(const Provenance &) const = default;
};
struct Image {
    std::string sha256;
    uint32_t size = 0;
    uint16_t origin = 0;
    bool operator==(const Image &) const = default;
};
[[nodiscard]] Result<Image> Identify(std::span<const uint8_t> bytes, uint16_t origin);
struct ImageLocation {
    std::string image;
    uint32_t offset = 0;
    bool operator==(const ImageLocation &) const = default;
};
struct ExternalLocation {
    std::string address_space = "z80:flat16";
    uint16_t address = 0;
    bool operator==(const ExternalLocation &) const = default;
};
struct NamedValue {
    uint32_t value = 0;
    uint8_t width = 16;
    bool operator==(const NamedValue &) const = default;
};
using Binding = std::variant<ImageLocation, ExternalLocation, NamedValue>;

enum class Field { Name, Kind, Extent, Summary, Inputs, Outputs, Clobbers, Questions, ExportName, Encoding };
using FieldValue = std::variant<std::string, SymbolType, std::optional<uint32_t>>;
struct Claim {
    FieldValue value;
    Provenance provenance;
    bool operator==(const Claim &) const = default;
};
struct SymbolRecord {
    SymbolId id;
    Binding binding;
    std::map<Field, Claim> fields;
    std::map<std::string, Provenance> aliases;
    std::vector<Claim> historical_names;
    bool retired = false;
    const std::string &Name() const;
    SymbolType Kind() const;
    std::optional<uint32_t> Extent() const;
    std::string Text(Field field) const;
    bool operator==(const SymbolRecord &) const = default;
};
struct Source {
    SourceId id;
    std::string key, sha256, attribution;
    bool operator==(const Source &) const = default;
};
struct Evidence {
    EvidenceId id;
    std::string image, source_key, content, limitations;
    std::optional<uint32_t> offset;
    std::vector<uint8_t> bytes;
    bool operator==(const Evidence &) const = default;
};
enum class Relation { Branch, Memory, Pointer };
struct Reference {
    ReferenceId id;
    uint32_t offset = 0;
    Relation relation = Relation::Branch;
    SymbolId target;
    uint32_t target_offset = 0;
    Provenance provenance;
    bool operator==(const Reference &) const = default;
};
struct Proposal {
    ProposalId id;
    SymbolId symbol;
    Field field = Field::Summary;
    Claim claim;
    // Stable analyzer/import assertion key: reruns retain the original outcome.
    std::string key;
    bool operator==(const Proposal &) const = default;
};
struct State {
    ProjectId id;
    std::string target = "z80";
    std::string address_space = "z80:flat16";
    Image image;
    std::map<SymbolId, SymbolRecord> symbols;
    std::map<SourceId, Source> sources;
    std::map<EvidenceId, Evidence> evidence;
    std::map<ReferenceId, Reference> references;
    std::map<ProposalId, Proposal> proposals;
    bool operator==(const State &) const = default;
};

class Project {
  public:
    [[nodiscard]] static Result<Project> New(const Image &image);
    [[nodiscard]] static Result<Project> Restore(State state);
    const State &Get() const noexcept { return state_; }
    [[nodiscard]] Result<SymbolId> Create(Binding binding, std::string name,
                                          SymbolType kind = SymbolType::Label,
                                          std::optional<uint32_t> extent = {}, std::string summary = {},
                                          Provenance provenance = {});
    [[nodiscard]] Result<void> Edit(SymbolId id, const std::map<Field, FieldValue> &changes,
                                    Provenance provenance = {});
    [[nodiscard]] Result<void> Alias(SymbolId id, std::string name, bool add, Provenance provenance = {});
    [[nodiscard]] Result<void> Retire(SymbolId id);
    [[nodiscard]] Result<SymbolId> Resolve(std::string_view name) const;
    std::vector<SymbolId> At(uint16_t address) const;
    std::optional<uint16_t> Address(const SymbolRecord &symbol) const;
    Binding Bind(uint16_t address) const;
    [[nodiscard]] Result<SourceId> ImportLegacy(std::string_view json, std::string source_key,
                                                std::string attribution);
    [[nodiscard]] Result<EvidenceId> Attach(Evidence evidence);
    [[nodiscard]] Result<ReferenceId> Refer(Reference reference);
    [[nodiscard]] Result<ProposalId> Propose(Proposal proposal);
    [[nodiscard]] Result<void> ReviewProposal(ProposalId id, Review review);
    [[nodiscard]] static Result<void> Validate(const State &state);

  private:
    explicit Project(State state) : state_(std::move(state)) {}
    State state_;
    [[nodiscard]] Result<void> Commit(State state);
};

std::string NewId(std::string_view prefix);
std::string FieldName(Field field);
[[nodiscard]] Result<Field> ParseField(std::string_view text);
} // namespace z80::dbg::analysis
