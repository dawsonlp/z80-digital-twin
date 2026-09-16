#include "analysis_export.h"
#include "pasmo_source.h"
#include <charconv>
#include <fstream>
#include <iostream>
#include <map>
#include <set>

namespace {
uint32_t number(std::string_view value, uint32_t maximum = 65535) {
    int base = 10;
    if (value.starts_with("0x") || value.starts_with("0X")) {
        value.remove_prefix(2);
        base = 16;
    } else if (value.starts_with('$')) {
        value.remove_prefix(1);
        base = 16;
    }
    uint32_t n = 0;
    auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), n, base);
    if (error != std::errc{} || end != value.data() + value.size() || n > maximum)
        throw std::invalid_argument("invalid address/range number");
    return n;
}
template <class T> T require(z80::dbg::analysis::Result<T> result) {
    if (!result)
        throw std::runtime_error(result.error().message);
    return std::move(*result);
}
void write_new(const std::string &path, const std::string &text) {
    // Artifact paths must be new; the manifest is published last. A manifest
    // authenticates the completed set. Existing output is never silently replaced.
    std::ofstream out(path, std::ios::binary | std::ios::noreplace);
    if (!out)
        throw std::runtime_error("cannot create output (it may already exist): " + path);
    out << text;
    out.close();
    if (!out)
        throw std::runtime_error("cannot write output: " + path);
}
} // namespace
int main(int argc, char **argv) try {
    using namespace z80::dbg;
    using namespace z80::dbg::analysis;
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
        std::cout << "Usage: z80_disassemble --org 0xADDR program.bin > program.asm\n"
                     "Semantic: --org ADDR program.bin --analysis project.z80analysis\n"
                     "          --output source.asm --map source.map.json --manifest source.manifest.json\n"
                     "          [--offset N --length N --aliases --select SYMBOL_ID ...]\n";
        return 0;
    }
    std::map<std::string, std::string> args;
    std::string input_path;
    const std::set<std::string> allowed = {"--org",    "--analysis", "--output",  "--map",   "--manifest",
                                           "--offset", "--length",   "--aliases", "--select"};
    ExportOptions options;
    for (int i = 1; i < argc; ++i) {
        std::string key = argv[i];
        if (!key.starts_with("--")) {
            if (!input_path.empty())
                throw std::invalid_argument("multiple input binaries");
            input_path = key;
            continue;
        }
        if (!allowed.contains(key) || args.contains(key))
            throw std::invalid_argument("unknown/repeated option: " + key);
        if (key == "--aliases") {
            options.aliases = true;
            args.emplace(key, "true");
            continue;
        }
        if (i + 1 == argc)
            throw std::invalid_argument("missing option value");
        const std::string value = argv[++i];
        if (key == "--select")
            options.selected.push_back({value});
        else
            args.emplace(key, value);
    }
    if (!args.contains("--org") || input_path.empty())
        throw std::invalid_argument("--org and input binary are required");
    const auto origin = static_cast<uint16_t>(number(args.at("--org")));
    auto text = require(ReadText(input_path));
    const auto bytes = std::span(reinterpret_cast<const uint8_t *>(text.data()), text.size());
    if (!args.contains("--analysis")) {
        if (args.size() != 1 || !options.selected.empty())
            throw std::invalid_argument("semantic output options require --analysis");
        std::cout << DisassemblePasmo(bytes, origin);
        if (!std::cout)
            throw std::runtime_error("cannot write source output");
        return 0;
    }
    for (auto key : {"--output", "--map", "--manifest"})
        if (!args.contains(key))
            throw std::invalid_argument(std::string("semantic export requires ") + key);
    std::set<std::filesystem::path> destinations;
    for (auto key : {"--output", "--map", "--manifest"}) {
        auto path = std::filesystem::absolute(args.at(key)).lexically_normal();
        if (!destinations.insert(path).second || std::filesystem::exists(path))
            throw std::invalid_argument("export requires three distinct new output paths");
    }
    auto image = require(Identify(bytes, origin));
    auto loaded = require(Open(args.at("--analysis"), image));
    if (args.contains("--offset"))
        options.offset = number(args.at("--offset"));
    if (args.contains("--length"))
        options.length = number(args.at("--length"));
    const auto artifacts = require(ExportPasmo(loaded.project, bytes, options));
    write_new(args.at("--output"), artifacts.assembly);
    write_new(args.at("--map"), artifacts.source_map);
    write_new(args.at("--manifest"), artifacts.manifest);
    return 0;
} catch (const std::exception &error) {
    std::cerr << "z80_disassemble: " << error.what() << '\n';
    return 1;
}
