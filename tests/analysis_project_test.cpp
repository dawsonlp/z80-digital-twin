// Copyright (c) 2026 Larry Dawson. Licensed under the MIT License (see LICENSE).
#include "analysis_workspace.h"
#include "content_hash.h"
#include <array>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <sys/file.h>
#include <unistd.h>
using namespace z80::dbg;
using namespace z80::dbg::analysis;
namespace {
int failures = 0;
void check(bool ok, const char *message) {
    if (!ok) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}
template <class T> T require(Result<T> result) {
    if (!result)
        throw std::runtime_error(result.error().message);
    return std::move(*result);
}
void require(Result<void> result) {
    if (!result)
        throw std::runtime_error(result.error().message);
}
void write(const std::filesystem::path &p, std::string_view text) {
    std::ofstream out(p, std::ios::binary);
    out << text;
    if (!out)
        throw std::runtime_error("test write");
}
struct Directory {
    std::filesystem::path path;
    Directory() {
        std::string pattern = "/tmp/z80-analysis-test.XXXXXX";
        auto p = ::mkdtemp(pattern.data());
        if (!p)
            throw std::runtime_error("mkdtemp");
        path = p;
    }
    ~Directory() {
        std::error_code e;
        std::filesystem::remove_all(path, e);
    }
};
json::Value &member(json::Value &j, const char *key) { return std::get<json::Value::Object>(j.data).at(key); }
} // namespace
int main() try {
    check(Sha256(std::string_view{}) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
          "SHA256 empty vector");
    check(Sha256(std::string_view("abc")) ==
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
          "SHA256 abc vector");
    check(Sha256(std::string_view(std::string(1000000, 'a'))) ==
              "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",
          "SHA256 multi-block vector");
    for (auto bad :
         {"{\"x\":1,\"x\":2}", "01", "+1", "1.0", "[1,]", "\"\\uD800\"", "\"\n\"", "{\"a\":false} true"}) {
        bool rejected = false;
        try {
            (void)json::Parse(bad);
        } catch (const std::invalid_argument &) {
            rejected = true;
        }
        check(rejected, "strict JSON rejects malformed/unsupported input");
    }
    check(json::Parse("\"\\uD83D\\uDE80\"").string() == "🚀", "Unicode surrogate pair retained");
    std::array<uint8_t, 32> bytes{};
    bytes[0] = 0xc3;
    bytes[1] = 0x08;
    bytes[2] = 0x80;
    auto image = require(Identify(bytes, 0x8000));
    auto project = require(Project::New(image));
    const auto entry =
        require(project.Create(project.Bind(0x8000), "ENTRY", SymbolType::Function, 8, "Loads STATE"));
    const auto state = require(project.Create(ExternalLocation{"z80:flat16", 0x9000}, "STATE",
                                              SymbolType::WordVariable, 2, "Persistent state"));
    const auto constant = require(project.Create(NamedValue{0x9000, 16}, "MAGIC"));
    (void)constant;
    require(project.Edit(
        entry, {{Field::Inputs, std::string("HL points to state")}, {Field::Name, std::string("START")}}));
    check(require(project.Resolve("ENTRY")) == entry && require(project.Resolve("START")) == entry,
          "rename retains identity and alias");
    check(project.Get().symbols.at(entry).Text(Field::Summary) == "Loads STATE" &&
              project.Get().symbols.at(entry).Extent() == 8,
          "edit preserves other fields");
    const auto snapshot = Serialize(project);
    check(!project.Edit(entry, {{Field::Name, std::string("STATE")}}) && Serialize(project) == snapshot,
          "conflicting rename is atomic");
    check(!project.Edit(entry, {{Field::Extent, std::optional<uint32_t>{33}}}) &&
              Serialize(project) == snapshot,
          "invalid extent is atomic");
    auto duplicate = require(project.Create(project.Bind(0x8000), "ALTERNATIVE"));
    check(project.At(0x8000).size() == 2 && !View(project).Lookup(0x8000),
          "ambiguous address does not pick an arbitrary symbol");
    require(project.Retire(duplicate));
    check(project.At(0x8000).size() == 1, "retired symbols are hidden without deleting identity");
    auto reference = require(project.Refer({{}, 0, Relation::Branch, entry, 0, {}}));
    require(project.Edit(entry, {{Field::Name, std::string("BOOT")}}));
    check(project.Get().references.at(reference).target == entry, "reference survives rename");
    Evidence ev{{},
                image.sha256,
                "fixture:runtime:run-1",
                "Observed instruction at entry",
                "One instruction is not a routine proof",
                0,
                {0xc3, 0x08, 0x80}};
    auto evidence = require(project.Attach(ev));
    auto proposal = require(project.Propose(
        {{},
         entry,
         Field::Summary,
         {std::string("Candidate purpose"), {Origin::Static, Review::Proposed, {}, {evidence}}},
         "fixture:purpose:1"}));
    require(project.ReviewProposal(proposal, Review::Rejected));
    auto rerun = require(project.Propose(
        {{},
         entry,
         Field::Summary,
         {std::string("Candidate purpose"), {Origin::Static, Review::Proposed, {}, {evidence}}},
         "fixture:purpose:1"}));
    check(rerun == proposal && project.Get().proposals.at(rerun).claim.provenance.review == Review::Rejected,
          "rerun preserves dismissed proposal");
    auto accepted = require(project.Propose(
        {{},
         state,
         Field::Summary,
         {std::string("Accepted interpretation"), {Origin::User, Review::Proposed, {}, {evidence}}},
         "fixture:purpose:2"}));
    require(project.ReviewProposal(accepted, Review::Accepted));
    check(project.Get().symbols.at(state).Text(Field::Summary) == "Accepted interpretation",
          "accept proposal updates only selected field");
    const auto serialized = Serialize(project);
    auto reopened = require(Deserialize(serialized, image));
    check(Serialize(reopened) == serialized && reopened.Get() == project.Get(),
          "save/reopen retains full identity provenance aliases references and proposals");
    check(require(reopened.Resolve("ENTRY")) == entry, "old name lookup survives reopen");
    auto other = image;
    other.origin = 0x7000;
    check(!Deserialize(serialized, other), "wrong image origin rejected");
    other = image;
    other.sha256[0] = other.sha256[0] == '0' ? '1' : '0';
    check(!Deserialize(serialized, other), "wrong image hash rejected");
    for (int mode = 0; mode < 5; ++mode) {
        auto root = json::Parse(serialized);
        auto &payload = member(root, "project");
        if (mode == 0)
            member(root, "version") = 2;
        if (mode == 1) {
            auto &list = std::get<json::Value::Array>(member(payload, "symbols").data);
            list.push_back(list.front());
        }
        if (mode == 2) {
            auto &list = std::get<json::Value::Array>(member(payload, "references").data);
            member(list.front(), "target") = NewId("sym_");
        }
        if (mode == 3)
            member(root, "revision") = "tampered";
        if (mode == 4)
            std::get<json::Value::Object>(payload.data).emplace("future_field", 1);
        if (mode != 3)
            member(root, "revision") = Sha256(json::Write(payload));
        check(!Deserialize(json::Write(root), image), "invalid/newer/dangling project rejected");
    }
    check(!Deserialize(serialized.substr(0, serialized.size() / 2), image), "truncated project rejected");
    const std::string legacy =
        R"({"version":1,"symbols":[{"address":"0x8010","name":"IMPORTED","type":"DATA_REGION","size":4,"description":"Unknown attribution"}]})";
    auto source = require(project.ImportLegacy(legacy, "fixture.sym", "unknown"));
    auto imported = require(project.Resolve("IMPORTED"));
    require(project.Edit(
        imported, {{Field::Name, std::string("REFINED")}, {Field::Summary, std::string("User refinement")}}));
    auto before = Serialize(project);
    check(require(project.ImportLegacy(legacy, "fixture.sym", "unknown")) == source &&
              Serialize(project) == before,
          "same-source import is idempotent after user refinement");
    check(!project.ImportLegacy(legacy + " ", "fixture.sym", "unknown") && Serialize(project) == before,
          "changed source does not overwrite user knowledge");
    check(!project.ImportLegacy(R"({"symbols":[{"name":"X","address":"0x8010junk"}]})", "bad.sym", "unknown"),
          "strict legacy address conversion");

    Directory dir;
    auto path = dir.path / "fixture.z80analysis";
    auto token = require(Save(project, path));
    check(require(Save(project, path, token)) == token && !std::filesystem::exists(path.string() + ".bak"),
          "no-op save stable and leaves recovery untouched");
    auto loaded = require(Open(path, image));
    check(loaded.disk_token == token, "open records disk token");
    require(project.Edit(entry, {{Field::Summary, std::string("Changed")}}));
    std::filesystem::create_directory(path.string() + ".bak");
    check(!Save(project, path, token) && require(ReadText(path)) == before,
          "failed backup/write preserves last valid file");
    for (const auto& item : std::filesystem::directory_iterator(dir.path))
        check(item.path().filename().string().find(".tmp.") == std::string::npos,
              "failed save cleans up temporary siblings");
    std::filesystem::remove(path.string() + ".bak");
    auto next_token = require(Save(project, path, token));
    check(require(ReadText(path.string() + ".bak")) == before,
          "successful save retains previous valid recovery copy");
    check(!Save(loaded.project, path, loaded.disk_token), "stale writer rejected");
    int lock = ::open((path.string() + ".lock").c_str(), O_RDWR);
    if (lock < 0 || ::flock(lock, LOCK_EX | LOCK_NB) != 0)
        throw std::runtime_error("test lock");
    check(!Save(project, path, next_token), "concurrent cooperating writer rejected");
    ::close(lock);
    write(path, "external edit");
    check(!Save(project, path, next_token) && require(ReadText(path)) == "external edit",
          "external disk edits preserved");

    Workspace workspace;
    require(workspace.Initialize(bytes, 0x8000));
    auto ws_id = require(workspace.Apply([&](Project &p) { return p.Create(p.Bind(0x8000), "WORK"); }));
    const auto active = workspace.CurrentRevision();
    check(workspace.Dirty() && !workspace.OpenFile(path) && workspace.CurrentRevision() == active,
          "dirty workspace cannot be silently replaced");
    auto ws_path = dir.path / "workspace.z80analysis";
    require(workspace.SaveFile(ws_path));
    require(workspace.Apply(
        [&](Project &p) { return p.Edit(ws_id, {{Field::Name, std::string("WORK_RENAMED")}}); }));
    require(workspace.SaveFile(ws_path));
    Workspace fresh;
    require(fresh.Initialize(bytes, 0x8000));
    require(fresh.OpenFile(ws_path));
    check(require(fresh.Active()->Resolve("WORK")) == ws_id &&
              fresh.Symbols().Resolve("WORK_RENAMED") == 0x8000,
          "workspace restart shares authoritative IDs and alias view");
    auto fresh_revision = fresh.CurrentRevision();
    write(dir.path / "broken", serialized.substr(0, 40));
    check(!fresh.OpenFile(dir.path / "broken") && fresh.CurrentRevision() == fresh_revision,
          "failed open preserves active project");
    require(project.Retire(entry));
    check(project.Get().references.at(reference).target == entry && project.Get().symbols.at(entry).retired,
          "retirement retains unresolved reference identity");
    {
        std::vector<uint8_t> full(65536);
        auto full_image = require(Identify(full, 0));
        auto full_project = require(Project::New(full_image));
        require(full_project.Create(full_project.Bind(0), "WHOLE", SymbolType::DataRegion, 65536));
        auto whole = View(full_project).FindContaining(0xffff);
        check(whole && whole->size == 65536, "read view preserves full address-space extent");
        auto label = require(full_project.Create(full_project.Bind(0x9000), "RST_38_IM1"));
        require(full_project.Edit(label, {{Field::Name, std::string("USER_IRQ")}}));
        auto view = View(full_project);
        check(!view.Lookup(0x38) && view.Resolve("RST_38_IM1") == 0x9000,
              "architectural fallback cannot shadow an alias");
        require(full_project.Retire(label));
        check(!View(full_project).Resolve("RST_38_IM1"), "retired alias cannot be resurrected as a fallback");
        auto snapshot = Serialize(full_project);
        check(!full_project.Edit(full_project.At(0).front(), {{Field::Summary, std::string("\xff")}}) &&
                  Serialize(full_project) == snapshot,
              "invalid UTF-8 cannot enter project state");
    }
    require(project.Edit(state, {{Field::Summary, std::string("Further user refinement")}}));
    check(project.Get().proposals.at(accepted).claim.provenance.review == Review::Superseded,
          "editing an accepted interpretation retains its superseded proposal");
    std::cout << "Analysis checks: " << failures << " failures\n";
    return failures ? 1 : 0;
} catch (const std::exception &e) {
    std::cerr << "test setup/error: " << e.what() << '\n';
    return 1;
}
