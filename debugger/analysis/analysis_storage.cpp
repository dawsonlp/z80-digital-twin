// Copyright (c) 2026 Larry Dawson. Licensed under the MIT License (see LICENSE).
#include "analysis_storage.h"
#include "content_hash.h"
#include <array>
#include <cerrno>
#include <charconv>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <limits>
#include <set>
#include <sys/file.h>
#include <sys/stat.h>
#include <system_error>
#include <unistd.h>
#include <utility>

namespace z80::dbg::analysis {
namespace {
using J = json::Value;
using O = J::Object;
using A = J::Array;
auto fail(ErrorCode code, std::string message) { return std::unexpected(Error{code, std::move(message)}); }
[[noreturn]] void invalid(std::string message) { throw std::invalid_argument(std::move(message)); }
uint32_t number(const J &v, uint32_t maximum = UINT32_MAX) {
    auto n = v.integer();
    if (n < 0 || static_cast<uint64_t>(n) > maximum)
        invalid("number outside field range");
    return static_cast<uint32_t>(n);
}
constexpr std::array origin_names = {"user", "imported", "static", "runtime", "generated"};
constexpr std::array review_names = {"proposed", "accepted", "rejected", "superseded"};
constexpr std::array relation_names = {"branch", "memory", "pointer"};
template <class Enum, size_t N> Enum enum_value(const J &value, const std::array<const char *, N> &names) {
    for (size_t i = 0; i < N; ++i)
        if (value.string() == names[i])
            return static_cast<Enum>(i);
    invalid("unknown enum value: " + value.string());
}
J provenance(const Provenance &p) {
    A evidence;
    for (const auto &id : p.evidence)
        evidence.emplace_back(id.value);
    return O{{"origin", origin_names.at(static_cast<size_t>(p.origin))},
             {"review", review_names.at(static_cast<size_t>(p.review))},
             {"source", p.source ? J(p.source->value) : J{}},
             {"evidence", std::move(evidence)}};
}
Provenance read_provenance(const J &j) {
    json::Keys(j, {"origin", "review", "source", "evidence"});
    Provenance p;
    p.origin = enum_value<Origin>(j.at("origin"), origin_names);
    p.review = enum_value<Review>(j.at("review"), review_names);
    if (!j.at("source").null())
        p.source = SourceId{j.at("source").string()};
    for (const auto &x : j.at("evidence").array())
        p.evidence.push_back({x.string()});
    return p;
}
J value(const FieldValue &v) {
    return std::visit(
        [](const auto &x) -> J {
            using T = std::decay_t<decltype(x)>;
            if constexpr (std::is_same_v<T, std::string>)
                return x;
            else if constexpr (std::is_same_v<T, SymbolType>)
                return ToString(x);
            else
                return x ? J(*x) : J{};
        },
        v);
}
FieldValue read_value(Field field, const J &v) {
    if (field == Field::Extent)
        return v.null() ? std::optional<uint32_t>{} : std::optional<uint32_t>{number(v, 65536)};
    if (field == Field::Kind) {
        auto kind = SymbolTypeFromString(v.string());
        if (!kind)
            invalid("unknown symbol kind");
        return *kind;
    }
    return v.string();
}
J claim(const Claim &c) { return O{{"value", value(c.value)}, {"provenance", provenance(c.provenance)}}; }
Claim read_claim(Field field, const J &j) {
    json::Keys(j, {"value", "provenance"});
    return {read_value(field, j.at("value")), read_provenance(j.at("provenance"))};
}
Field read_field(std::string_view name) {
    auto field = ParseField(name);
    if (!field)
        invalid(field.error().message);
    return *field;
}
J binding(const Binding &b) {
    return std::visit(
        [](const auto &x) -> J {
            using T = std::decay_t<decltype(x)>;
            if constexpr (std::is_same_v<T, ImageLocation>)
                return O{{"type", "image"}, {"image", x.image}, {"offset", x.offset}};
            else if constexpr (std::is_same_v<T, ExternalLocation>)
                return O{
                    {"type", "external"}, {"address_space", x.address_space}, {"address", int(x.address)}};
            else
                return O{{"type", "value"}, {"value", x.value}, {"width", int(x.width)}};
        },
        b);
}
Binding read_binding(const J &j) {
    const auto &type = j.at("type").string();
    if (type == "image") {
        json::Keys(j, {"type", "image", "offset"});
        return ImageLocation{j.at("image").string(), number(j.at("offset"), 65535)};
    }
    if (type == "external") {
        json::Keys(j, {"type", "address_space", "address"});
        return ExternalLocation{j.at("address_space").string(),
                                static_cast<uint16_t>(number(j.at("address"), 65535))};
    }
    if (type == "value") {
        json::Keys(j, {"type", "value", "width"});
        return NamedValue{number(j.at("value")), static_cast<uint8_t>(number(j.at("width"), 32))};
    }
    invalid("unknown binding type");
}
template <class Map, class Record> void unique(Map &map, Record record) {
    const auto id = record.id;
    if (!map.emplace(id, std::move(record)).second)
        invalid("duplicate record ID");
}
State read_state(const J &j) {
    json::Keys(j, {"id", "target", "address_space", "image", "symbols", "sources", "evidence", "references",
                   "proposals"});
    State s;
    s.id = {j.at("id").string()};
    s.target = j.at("target").string();
    s.address_space = j.at("address_space").string();
    const auto &img = j.at("image");
    json::Keys(img, {"sha256", "size", "origin"});
    s.image = {img.at("sha256").string(), number(img.at("size"), 65536),
               static_cast<uint16_t>(number(img.at("origin"), 65535))};
    for (const auto &x : j.at("sources").array()) {
        json::Keys(x, {"id", "key", "sha256", "attribution"});
        unique(s.sources, Source{{x.at("id").string()},
                                 x.at("key").string(),
                                 x.at("sha256").string(),
                                 x.at("attribution").string()});
    }
    for (const auto &x : j.at("symbols").array()) {
        json::Keys(x, {"id", "binding", "fields", "aliases", "historical_names", "retired"});
        SymbolRecord sym;
        sym.id = {x.at("id").string()};
        sym.binding = read_binding(x.at("binding"));
        sym.retired = x.at("retired").boolean();
        for (const auto &[key, c] : x.at("fields").object()) {
            auto field = read_field(key);
            sym.fields.emplace(field, read_claim(field, c));
        }
        for (const auto &[key, p] : x.at("aliases").object())
            sym.aliases.emplace(key, read_provenance(p));
        for (const auto &c : x.at("historical_names").array())
            sym.historical_names.push_back(read_claim(Field::Name, c));
        unique(s.symbols, std::move(sym));
    }
    for (const auto &x : j.at("evidence").array()) {
        json::Keys(x, {"id", "image", "source_key", "content", "limitations", "offset", "bytes"});
        Evidence e;
        e.id = {x.at("id").string()};
        e.image = x.at("image").string();
        e.source_key = x.at("source_key").string();
        e.content = x.at("content").string();
        e.limitations = x.at("limitations").string();
        if (!x.at("offset").null())
            e.offset = number(x.at("offset"), 65535);
        for (const auto &b : x.at("bytes").array())
            e.bytes.push_back(static_cast<uint8_t>(number(b, 255)));
        unique(s.evidence, std::move(e));
    }
    for (const auto &x : j.at("references").array()) {
        json::Keys(x, {"id", "offset", "relation", "target", "target_offset", "provenance"});
        unique(s.references, Reference{{x.at("id").string()},
                                       number(x.at("offset"), 65535),
                                       enum_value<Relation>(x.at("relation"), relation_names),
                                       {x.at("target").string()},
                                       number(x.at("target_offset"), 65535),
                                       read_provenance(x.at("provenance"))});
    }
    for (const auto &x : j.at("proposals").array()) {
        json::Keys(x, {"id", "symbol", "field", "claim", "key"});
        auto f = read_field(x.at("field").string());
        unique(s.proposals, Proposal{{x.at("id").string()},
                                     {x.at("symbol").string()},
                                     f,
                                     read_claim(f, x.at("claim")),
                                     x.at("key").string()});
    }
    return s;
}
struct File {
    int fd = -1;
    explicit File(int handle) : fd(handle) {}
    File(const File &) = delete;
    File &operator=(const File &) = delete;
    ~File() {
        if (fd >= 0)
            ::close(fd);
    }
};
struct Temporary {
    const std::string& path;
    bool active = true;
    ~Temporary() { if (active) ::unlink(path.c_str()); }
};

Result<void> replace(const std::filesystem::path &path, std::string_view text, bool create_only = false) {
    std::string pattern = path.string() + ".tmp.XXXXXX";
    File file(::mkstemp(pattern.data()));
    if (file.fd < 0)
        return fail(ErrorCode::Io, "cannot create temporary sibling: " + std::string(std::strerror(errno)));
    Temporary temporary{pattern};
    size_t written = 0;
    while (written < text.size()) {
        auto n = ::write(file.fd, text.data() + written, text.size() - written);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return fail(ErrorCode::Io, "cannot write complete analysis file");
        written += static_cast<size_t>(n);
    }
    if (::fsync(file.fd) != 0)
        return fail(ErrorCode::Io, "cannot sync analysis file");
    const int descriptor = std::exchange(file.fd, -1);
    if (::close(descriptor) != 0)
        return fail(ErrorCode::Io, "cannot close analysis file");
    if (create_only) {
        if (::link(temporary.path.c_str(), path.c_str()) != 0)
            return fail(errno == EEXIST ? ErrorCode::Conflict : ErrorCode::Io,
                        "cannot publish new analysis file without overwriting");
        // The temporary-name guard removes only the extra hard link.
    } else {
        if (::rename(temporary.path.c_str(), path.c_str()) != 0)
            return fail(ErrorCode::Io, "cannot atomically replace analysis file");
        temporary.active = false;
    }
    return {};
}
} // namespace
J ProjectJson(const Project &project) {
    const auto &s = project.Get();
    A symbols, sources, evidence, references, proposals;
    for (const auto &[id, sym] : s.symbols) {
        O fields, aliases;
        A history;
        for (const auto &[f, c] : sym.fields)
            fields.emplace(FieldName(f), claim(c));
        for (const auto &[name, p] : sym.aliases)
            aliases.emplace(name, provenance(p));
        for (const auto &c : sym.historical_names)
            history.push_back(claim(c));
        symbols.emplace_back(O{{"id", id.value},
                               {"binding", binding(sym.binding)},
                               {"fields", std::move(fields)},
                               {"aliases", std::move(aliases)},
                               {"historical_names", std::move(history)},
                               {"retired", sym.retired}});
    }
    for (const auto &[id, s] : s.sources)
        sources.emplace_back(
            O{{"id", id.value}, {"key", s.key}, {"sha256", s.sha256}, {"attribution", s.attribution}});
    for (const auto &[id, e] : s.evidence) {
        A bytes;
        for (auto byte : e.bytes)
            bytes.emplace_back(int(byte));
        evidence.emplace_back(O{{"id", id.value},
                                {"image", e.image},
                                {"source_key", e.source_key},
                                {"content", e.content},
                                {"limitations", e.limitations},
                                {"offset", e.offset ? J(*e.offset) : J{}},
                                {"bytes", std::move(bytes)}});
    }
    for (const auto &[id, r] : s.references)
        references.emplace_back(O{{"id", id.value},
                                  {"offset", r.offset},
                                  {"relation", relation_names.at(static_cast<size_t>(r.relation))},
                                  {"target", r.target.value},
                                  {"target_offset", r.target_offset},
                                  {"provenance", provenance(r.provenance)}});
    for (const auto &[id, p] : s.proposals)
        proposals.emplace_back(O{{"id", id.value},
                                 {"symbol", p.symbol.value},
                                 {"field", FieldName(p.field)},
                                 {"claim", claim(p.claim)},
                                 {"key", p.key}});
    return O{
        {"id", s.id.value},
        {"target", s.target},
        {"address_space", s.address_space},
        {"image", O{{"sha256", s.image.sha256}, {"size", s.image.size}, {"origin", int(s.image.origin)}}},
        {"symbols", std::move(symbols)},
        {"sources", std::move(sources)},
        {"evidence", std::move(evidence)},
        {"references", std::move(references)},
        {"proposals", std::move(proposals)}};
}
std::string Revision(const Project &project) { return Sha256(json::Write(ProjectJson(project))); }
std::string Serialize(const Project &project) {
    auto payload = ProjectJson(project);
    auto revision = Sha256(json::Write(payload));
    return json::Write(O{{"format", "z80-analysis"},
                         {"version", 1},
                         {"revision", std::move(revision)},
                         {"project", std::move(payload)}});
}
Result<Project> Deserialize(std::string_view text, const Image &expected) {
    try {
        auto root = json::Parse(text);
        json::Keys(root, {"format", "version", "revision", "project"});
        if (root.at("format").string() != "z80-analysis" || root.at("version").integer() != 1)
            return fail(ErrorCode::Unsupported, "unsupported analysis format/version");
        auto state = read_state(root.at("project"));
        if (state.image != expected)
            return fail(ErrorCode::ImageMismatch,
                        "analysis belongs to different image bytes, length or origin");
        auto restored = Project::Restore(std::move(state));
        if (!restored)
            return restored;
        if (Revision(*restored) != root.at("revision").string())
            return fail(ErrorCode::Invalid, "analysis revision hash does not match content");
        return restored;
    } catch (const std::invalid_argument &error) {
        return fail(ErrorCode::Invalid, error.what());
    }
}
Result<std::string> ReadText(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return fail(ErrorCode::Io, "cannot open " + path.string());
    std::string text;
    std::array<char, 8192> buffer{};
    while (file) {
        file.read(buffer.data(), buffer.size());
        text.append(buffer.data(), static_cast<size_t>(file.gcount()));
        if (text.size() > 16 * 1024 * 1024)
            return fail(ErrorCode::Invalid, "file exceeds 16 MiB");
    }
    if (!file.eof())
        return fail(ErrorCode::Io, "cannot read " + path.string());
    return text;
}
Result<Loaded> Open(const std::filesystem::path &path, const Image &expected) {
    auto text = ReadText(path);
    if (!text)
        return std::unexpected(text.error());
    auto project = Deserialize(*text, expected);
    if (!project)
        return std::unexpected(project.error());
    return Loaded{std::move(*project), Sha256(*text)};
}
Result<std::string> Save(const Project &project, const std::filesystem::path &path,
                         const std::optional<std::string> &token) {
    if (auto valid = Project::Validate(project.Get()); !valid)
        return std::unexpected(valid.error());
    const auto serialized = Serialize(project); // All allocations for content precede publication.
    auto new_token = Sha256(serialized);
    if (serialized.size() > 16 * 1024 * 1024)
        return fail(ErrorCode::Invalid, "project exceeds 16 MiB storage limit");
    std::filesystem::path parent =
        path.parent_path().empty() ? std::filesystem::path(".") : path.parent_path();
    File directory(::open(parent.c_str(), O_RDONLY | O_DIRECTORY));
    if (directory.fd < 0)
        return fail(ErrorCode::Io, "cannot open destination directory");
    const auto lock_path = path.string() + ".lock";
    File lock(::open(lock_path.c_str(), O_CREAT | O_RDWR | O_NOFOLLOW, 0600));
    if (lock.fd < 0 || ::flock(lock.fd, LOCK_EX | LOCK_NB) != 0)
        return fail(ErrorCode::Conflict, "analysis file is locked or lock cannot be acquired");
    struct stat st{};
    const bool exists = ::lstat(path.c_str(), &st) == 0;
    if (!exists && errno != ENOENT)
        return fail(ErrorCode::Io, "cannot inspect destination");
    if (exists && !S_ISREG(st.st_mode))
        return fail(ErrorCode::Conflict, "destination is not a regular file");
    if (exists != token.has_value())
        return fail(ErrorCode::Conflict, "destination appeared or disappeared; reopen or choose a new path");
    std::optional<std::string> old;
    if (exists) {
        auto text = ReadText(path);
        if (!text)
            return std::unexpected(text.error());
        if (Sha256(*text) != *token)
            return fail(ErrorCode::Conflict, "analysis changed on disk; reopen or save to a new path");
        const auto prior = Deserialize(*text, project.Get().image);
        if (!prior || prior->Get().id != project.Get().id)
            return fail(ErrorCode::Conflict,
                        "existing analysis is invalid or belongs to another project; choose a new path");
        old = std::move(*text);
        if (*old == serialized)
            return *token;
        if (auto backup = replace(path.string() + ".bak", *old); !backup)
            return std::unexpected(backup.error());
        // Recheck non-cooperating edits after staging the recovery copy.
        auto current = ReadText(path);
        if (!current || Sha256(*current) != *token)
            return fail(ErrorCode::Conflict, "analysis changed during save");
    }
    if (auto result = replace(path, serialized, !exists); !result)
        return std::unexpected(result.error());
    if (::fsync(directory.fd) != 0)
        return fail(ErrorCode::Io, "file replaced, but directory sync failed; reopen to verify durability");
    return new_token;
}

Result<SourceId> Project::ImportLegacy(std::string_view text, std::string key, std::string attribution) {
    const auto hash = Sha256(text);
    for (const auto &[id, source] : state_.sources)
        if (source.key == key) {
            if (source.sha256 == hash)
                return id;
            return fail(ErrorCode::Conflict, "source changed since import; review it as a separate proposal");
        }
    try {
        const auto root = json::Parse(text);
        // Legacy optional fields are accepted, but unknown fields are rejected
        // so this conversion cannot silently consume a richer/newer format.
        for (const auto &[name, v] : root.object())
            if (name != "version" && name != "program" && name != "symbols")
                invalid("unknown legacy field: " + name);
        if (auto v = root.object().find("version"); v != root.object().end() && v->second.integer() != 1)
            invalid("unsupported legacy version");
        State next = state_;
        Source source{{NewId("src_")}, std::move(key), hash, std::move(attribution)};
        const auto source_id = source.id;
        next.sources.emplace(source_id, std::move(source));
        Provenance p{Origin::Imported, Review::Accepted, source_id, {}};
        size_t entry = 0;
        for (const auto &x : root.at("symbols").array()) {
            ++entry;
            for (const auto &[name, v] : x.object())
                if (name != "name" && name != "address" && name != "type" && name != "description" &&
                    name != "size")
                    invalid("unknown legacy symbol field");
            uint32_t address = 0;
            const auto &a = x.at("address");
            if (std::holds_alternative<std::string>(a.data)) {
                std::string_view value = a.string();
                int base = 10;
                if (value.starts_with("0x") || value.starts_with("0X")) {
                    value.remove_prefix(2);
                    base = 16;
                }
                const auto [end, error] =
                    std::from_chars(value.data(), value.data() + value.size(), address, base);
                if (error != std::errc{} || end != value.data() + value.size() || address > 65535)
                    invalid("invalid legacy address at entry " + std::to_string(entry));
            } else
                address = number(a, 65535);
            SymbolRecord sym;
            sym.id = {NewId("sym_")};
            sym.binding = Bind(static_cast<uint16_t>(address));
            const auto &obj = x.object();
            auto kind = SymbolType::Label;
            if (auto t = obj.find("type"); t != obj.end()) {
                auto parsed = SymbolTypeFromString(t->second.string());
                if (!parsed)
                    invalid("invalid legacy type");
                kind = *parsed;
            }
            uint32_t size = 1;
            if (auto e = obj.find("size"); e != obj.end())
                size = number(e->second, 65536);
            std::string summary;
            if (auto d = obj.find("description"); d != obj.end())
                summary = d->second.string();
            sym.fields = {{Field::Name, {x.at("name").string(), p}},
                          {Field::Kind, {kind, p}},
                          {Field::Extent, {std::optional<uint32_t>(size), p}},
                          {Field::Summary, {summary, p}}};
            // Entry ordinal is retained as evidence; no identity inferred from name/address.
            Evidence evidence{{NewId("ev_")},
                              state_.image.sha256,
                              next.sources.at(source_id).key,
                              "Legacy entry " + std::to_string(entry) + ": " + json::Write(x),
                              "Imported attribution; not independently verified",
                              {},
                              {}};
            auto eid = evidence.id;
            next.evidence.emplace(eid, std::move(evidence));
            for (auto &[field, claim] : sym.fields)
                claim.provenance.evidence.push_back(eid);
            unique(next.symbols, std::move(sym));
        }
        if (auto result = Commit(std::move(next)); !result)
            return std::unexpected(result.error());
        return source_id;
    } catch (const std::invalid_argument &error) {
        return fail(ErrorCode::Invalid, error.what());
    }
}
} // namespace z80::dbg::analysis
