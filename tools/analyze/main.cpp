// Copyright (c) 2026 Larry Dawson. Licensed under the MIT License (see LICENSE).
#include "analysis_workspace.h"
#include "transfer_analysis.h"
#include <charconv>
#include <iostream>
#include <map>
#include <set>

using namespace z80::dbg;
using namespace z80::dbg::analysis;
namespace {
template <class T> T require(Result<T> result) {
    if (!result)
        throw std::runtime_error(result.error().message);
    return std::move(*result);
}
void require(Result<void> result) {
    if (!result)
        throw std::runtime_error(result.error().message);
}
uint32_t number(std::string_view text, uint32_t maximum = 65535) {
    int base = 10;
    if (text.starts_with("0x")) {
        text.remove_prefix(2);
        base = 16;
    } else if (text.starts_with('$')) {
        text.remove_prefix(1);
        base = 16;
    }
    uint32_t n = 0;
    auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), n, base);
    if (ec != std::errc{} || end != text.data() + text.size() || n > maximum)
        throw std::invalid_argument("invalid number");
    return n;
}
void usage() {
    std::cout
        << "Usage: z80_analyze COMMAND --image file.bin --org 0xADDR --project file.z80analysis [options]\n"
           "       z80_analyze transfers --source capture.json [--through effects|continuations|values|constructed]\n"
           "  new                           create an image-bound project (will not overwrite)\n"
           "  show                          print the authoritative JSON\n"
           "  create --name NAME (--address ADDR | --value N [--width 8|16|32])\n"
           "         [--kind LABEL|FUNCTION|... --extent N --summary TEXT]\n"
           "  rename --symbol ID_OR_NAME --name NEW_NAME\n"
           "  set --symbol ID_OR_NAME --field FIELD --value VALUE\n"
           "  alias --symbol ID_OR_NAME --name ALIAS [--remove]\n"
           "  retire --symbol ID_OR_NAME\n"
           "  reference --offset N --relation branch|memory|pointer --symbol ID_OR_NAME [--addend N]\n"
           "  import --source file.sym [--attribution TEXT]\n"
           "Fields: name, kind, extent (N or unknown), summary, inputs, outputs, clobbers,\n"
           "        questions, export_name, encoding (bytes|words|text|pointers).\n";
}
} // namespace
int main(int argc, char **argv) try {
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
        usage();
        return 0;
    }
    if (argc < 2) {
        usage();
        return 1;
    }
    const std::string command = argv[1];
    if (command == "transfers") {
        std::map<std::string, std::string> arguments;
        for (int i = 2; i < argc; i += 2) {
            const std::string key = argv[i];
            if ((key != "--source" && key != "--through") || arguments.contains(key) || i + 1 >= argc)
                throw std::invalid_argument("usage: z80_analyze transfers --source capture.json [--through effects|continuations|values|constructed]");
            arguments.emplace(key, argv[i + 1]);
        }
        if (!arguments.contains("--source")) throw std::invalid_argument("missing --source");
        AnalysisStage stage = AnalysisStage::Constructed;
        if (arguments.contains("--through")) {
            const auto& name = arguments.at("--through");
            if (name == "effects") stage = AnalysisStage::Effects;
            else if (name == "continuations") stage = AnalysisStage::Continuations;
            else if (name == "values") stage = AnalysisStage::Values;
            else if (name != "constructed") throw std::invalid_argument("unknown analysis stage: " + name);
        }
        const auto capture = require(ReadTransferCapture(require(ReadText(arguments.at("--source")))));
        std::cout << require(TransferReport(capture, stage));
        return std::cout ? 0 : 1;
    }
    std::map<std::string, std::string> options;
    const std::set<std::string> allowed = {
        "--image",  "--org",         "--project", "--name",    "--address",  "--value",
        "--width",  "--kind",        "--extent",  "--summary", "--symbol",   "--field",
        "--source", "--attribution", "--remove",  "--offset",  "--relation", "--addend"};
    for (int i = 2; i < argc; ++i) {
        std::string key = argv[i];
        if (!allowed.contains(key) || options.contains(key))
            throw std::invalid_argument("unknown or repeated option: " + key);
        if (key == "--remove")
            options.emplace(key, "true");
        else {
            if (i + 1 == argc)
                throw std::invalid_argument("missing option value");
            options.emplace(key, argv[++i]);
        }
    }
    auto get = [&](const char *key) -> const std::string & {
        auto p = options.find(key);
        if (p == options.end())
            throw std::invalid_argument(std::string("missing ") + key);
        return p->second;
    };
    auto text = require(ReadText(get("--image")));
    Workspace workspace;
    require(workspace.Initialize(std::span(reinterpret_cast<const uint8_t *>(text.data()), text.size()),
                                 static_cast<uint16_t>(number(get("--org")))));
    const auto path = get("--project");
    if (command == "new") {
        require(workspace.SaveFile(path));
        return 0;
    }
    require(workspace.OpenFile(path));
    if (command == "show") {
        std::cout << Serialize(*workspace.Active());
        return std::cout ? 0 : 1;
    }
    auto symbol = [&] {
        const auto &key = get("--symbol");
        if (workspace.Active()->Get().symbols.contains(SymbolId{key}))
            return SymbolId{key};
        return require(workspace.Active()->Resolve(key));
    };
    if (command == "create") {
        if (options.contains("--address") == options.contains("--value"))
            throw std::invalid_argument("choose address or value binding");
        auto created = require(workspace.Apply([&](Project &p) -> Result<SymbolId> {
            Binding binding =
                options.contains("--address")
                    ? p.Bind(static_cast<uint16_t>(number(get("--address"))))
                    : Binding(NamedValue{number(get("--value"), UINT32_MAX),
                                         static_cast<uint8_t>(
                                             options.contains("--width") ? number(get("--width"), 32) : 16)});
            auto kind = options.contains("--kind") ? SymbolTypeFromString(get("--kind"))
                                                   : std::optional<SymbolType>(SymbolType::Label);
            if (!kind)
                throw std::invalid_argument("unknown symbol kind");
            return p.Create(std::move(binding), get("--name"), *kind,
                            options.contains("--extent")
                                ? std::optional<uint32_t>(number(get("--extent"), 65536))
                                : std::nullopt,
                            options.contains("--summary") ? get("--summary") : std::string{});
        }));
        require(workspace.SaveFile(path));
        std::cout << created.value << '\n';
        return 0;
    }
    if (command == "reference") {
        auto target = symbol();
        const auto relation = get("--relation");
        if (relation != "branch" && relation != "memory" && relation != "pointer")
            throw std::invalid_argument("unknown reference relation");
        auto created = require(workspace.Apply([&](Project &p) {
            return p.Refer({{},
                            number(get("--offset")),
                            relation == "branch"   ? Relation::Branch
                            : relation == "memory" ? Relation::Memory
                                                   : Relation::Pointer,
                            target,
                            options.contains("--addend") ? number(get("--addend")) : 0,
                            {}});
        }));
        require(workspace.SaveFile(path));
        std::cout << created.value << '\n';
        return 0;
    }
    if (command == "import")
        require(workspace.ImportFile(get("--source"),
                                     options.contains("--attribution") ? get("--attribution") : "unknown"));
    else if (command == "retire") {
        auto id = symbol();
        require(workspace.Apply([&](Project &p) { return p.Retire(id); }));
    } else if (command == "alias") {
        auto id = symbol();
        require(workspace.Apply(
            [&](Project &p) { return p.Alias(id, get("--name"), !options.contains("--remove")); }));
    } else if (command == "rename" || command == "set") {
        auto id = symbol();
        auto field = command == "rename" ? Field::Name : require(ParseField(get("--field")));
        const auto &input = command == "rename" ? get("--name") : get("--value");
        FieldValue value = input;
        if (field == Field::Kind) {
            auto kind = SymbolTypeFromString(input);
            if (!kind)
                throw std::invalid_argument("unknown symbol kind");
            value = *kind;
        }
        if (field == Field::Extent)
            value = input == "unknown" ? std::optional<uint32_t>{}
                                       : std::optional<uint32_t>{number(input, 65536)};
        require(workspace.Apply([&](Project &p) { return p.Edit(id, {{field, value}}); }));
    } else
        throw std::invalid_argument("unknown command: " + command);
    require(workspace.SaveFile(path));
    return 0;
} catch (const std::exception &error) {
    std::cerr << "z80_analyze: " << error.what() << '\n';
    return 1;
}
