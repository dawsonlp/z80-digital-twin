//
// Z80 Digital Twin - external CPU suite runner
// Copyright (c) 2025-2026 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//
// Runs freely available Z80 exerciser binaries as local assets. The first
// adapter is CP/M .COM: load at 0x0100, intercept BDOS CALL 0005h console
// output, and judge the emitted report. Missing assets are SKIP so a clean
// checkout remains green.
//

#include "z80_cpu.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

enum class Result {
    kPass = 0,
    kFail = 1,
    kHarnessError = 2,
    kSkip = 77,
};

struct Case {
    std::string name;
    std::string adapter;
    std::string asset;
    std::vector<std::string> expected_output;
    std::vector<std::string> reject_output;
    uint64_t timeout_instructions = 200'000'000;
};

struct Options {
    std::string case_name = "zexdoc";
    std::string assets_root;
    std::string artifact_root = "build/compat-artifacts/cpu";
    uint64_t timeout_instructions = 0;
    bool list = false;
};

std::vector<Case> cases() {
    return {
        {
            "zexdoc",
            "cpm_com",
            "cpu/zexdoc.com",
            {"Z80", "OK"},
            {"ERROR", "FAILED", "FAIL"},
            10'000'000'000,
        },
        {
            "zexall",
            "cpm_com",
            "cpu/zexall.com",
            {"Z80", "OK"},
            {"ERROR", "FAILED", "FAIL"},
            10'000'000'000,
        },
    };
}

void usage(const char* prog) {
    std::cout
        << "CPU correctness suite runner\n\n"
        << "Usage:\n"
        << "  " << prog << " --case zexdoc [--assets DIR] [--artifacts DIR]\n"
        << "  " << prog << " --case zexdoc --timeout-instructions N\n"
        << "  " << prog << " --list\n\n"
        << "Environment:\n"
        << "  Z80_COMPAT_ASSETS   root for external assets, e.g. cpu/zexdoc.com\n\n"
        << "Exit codes:\n"
        << "  0 pass, 1 suite failed, 2 harness error, 77 skipped\n";
}

Options parse_args(int argc, char** argv) {
    Options opt;
    if (const char* env = std::getenv("Z80_COMPAT_ASSETS")) opt.assets_root = env;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "-h" || a == "--help") {
            usage(argv[0]);
            std::exit(static_cast<int>(Result::kPass));
        } else if (a == "--list") {
            opt.list = true;
        } else if (a == "--case" && i + 1 < argc) {
            opt.case_name = argv[++i];
        } else if (a == "--assets" && i + 1 < argc) {
            opt.assets_root = argv[++i];
        } else if (a == "--artifacts" && i + 1 < argc) {
            opt.artifact_root = argv[++i];
        } else if (a == "--timeout-instructions" && i + 1 < argc) {
            opt.timeout_instructions = std::stoull(argv[++i]);
        } else {
            std::cerr << "Unknown or incomplete argument: " << a << "\n";
            std::exit(static_cast<int>(Result::kHarnessError));
        }
    }
    return opt;
}

std::vector<uint8_t> read_file(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)),
                                std::istreambuf_iterator<char>());
}

void write_text_file(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << text;
}

std::string upper_copy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

bool contains_all(const std::string& haystack, const std::vector<std::string>& needles) {
    const std::string h = upper_copy(haystack);
    for (const std::string& needle : needles) {
        if (h.find(upper_copy(needle)) == std::string::npos) return false;
    }
    return true;
}

bool contains_any(const std::string& haystack, const std::vector<std::string>& needles) {
    const std::string h = upper_copy(haystack);
    for (const std::string& needle : needles) {
        if (h.find(upper_copy(needle)) != std::string::npos) return true;
    }
    return false;
}

std::string hex16(uint16_t v) {
    std::ostringstream os;
    os << "0x" << std::uppercase << std::hex << std::setw(4) << std::setfill('0') << v;
    return os.str();
}

uint16_t pop_word(z80::CPU& cpu) {
    const uint16_t sp = cpu.SP();
    const uint16_t value = static_cast<uint16_t>(cpu.ReadMemory(sp) |
                                                 (cpu.ReadMemory(sp + 1) << 8));
    cpu.SP() = static_cast<uint16_t>(sp + 2);
    return value;
}

std::string read_dollar_string(const z80::CPU& cpu, uint16_t addr, std::size_t limit = 4096) {
    std::string out;
    for (std::size_t i = 0; i < limit; ++i) {
        const uint8_t ch = cpu.ReadMemory(static_cast<uint16_t>(addr + i));
        if (ch == '$') return out;
        out.push_back(static_cast<char>(ch));
    }
    out += "\n[HARNESS: unterminated BDOS 09 string]\n";
    return out;
}

void append_printable(std::string& out, char ch) {
    if (ch == '\r') {
        out.push_back('\n');
    } else if (ch == '\n' || ch == '\t' || (static_cast<unsigned char>(ch) >= 0x20 &&
                                            static_cast<unsigned char>(ch) < 0x7F)) {
        out.push_back(ch);
    }
}

struct RunReport {
    Result result = Result::kHarnessError;
    std::string reason;
    std::string output;
    uint64_t instructions = 0;
    uint64_t tstates = 0;
    uint16_t pc = 0;
    uint16_t sp = 0;
};

RunReport run_cpm_com(const Case& c, const std::vector<uint8_t>& program) {
    RunReport report;
    z80::CPU cpu;
    cpu.Reset();
    cpu.LoadProgram(program, 0x0100);
    cpu.PC() = 0x0100;
    cpu.SP() = 0xF000;

    bool terminated = false;
    std::array<uint16_t, 16> recent_pc{};
    std::size_t recent_i = 0;

    while (report.instructions < c.timeout_instructions) {
        if (cpu.PC() == 0x0000) {
            terminated = true;
            report.reason = "CP/M warm boot at 0000h";
            break;
        }

        if (cpu.PC() == 0x0005) {
            const uint16_t ret = pop_word(cpu);
            switch (cpu.C()) {
            case 0x00:
                terminated = true;
                report.reason = "BDOS system reset";
                break;
            case 0x02:
                append_printable(report.output, static_cast<char>(cpu.E()));
                break;
            case 0x09:
                for (char ch : read_dollar_string(cpu, cpu.DE())) append_printable(report.output, ch);
                break;
            default:
                report.result = Result::kHarnessError;
                report.reason = "unsupported BDOS call C=" + hex16(cpu.C()).substr(2);
                report.pc = cpu.PC();
                report.sp = cpu.SP();
                report.tstates = cpu.GetCycleCount();
                return report;
            }
            cpu.PC() = ret;
            if (terminated) break;
            continue;
        }

        recent_pc[recent_i++ % recent_pc.size()] = cpu.PC();
        do { cpu.Step(); } while (!cpu.InstructionComplete());
        ++report.instructions;
    }

    report.pc = cpu.PC();
    report.sp = cpu.SP();
    report.tstates = cpu.GetCycleCount();

    if (!terminated && report.instructions >= c.timeout_instructions) {
        std::ostringstream os;
        os << "timeout at PC=" << hex16(report.pc) << ", recent PCs:";
        for (uint16_t pc : recent_pc) os << " " << hex16(pc);
        report.reason = os.str();
        report.result = Result::kFail;
        return report;
    }

    if (contains_any(report.output, c.reject_output)) {
        report.result = Result::kFail;
        report.reason = "reject token found in output";
        return report;
    }
    if (!contains_all(report.output, c.expected_output)) {
        report.result = Result::kFail;
        report.reason = "expected output tokens not found";
        return report;
    }

    report.result = Result::kPass;
    report.reason = terminated ? report.reason : "expected output observed";
    return report;
}

std::string report_text(const Case& c, const std::filesystem::path& asset, const RunReport& r) {
    std::ostringstream os;
    os << "case: " << c.name << "\n"
       << "adapter: " << c.adapter << "\n"
       << "asset: " << asset.string() << "\n"
       << "result: " << static_cast<int>(r.result) << "\n"
       << "reason: " << r.reason << "\n"
       << "instructions: " << r.instructions << "\n"
       << "tstates: " << r.tstates << "\n"
       << "pc: " << hex16(r.pc) << "\n"
       << "sp: " << hex16(r.sp) << "\n"
       << "\n--- console ---\n"
       << r.output << "\n";
    return os.str();
}

} // namespace

int main(int argc, char** argv) {
    const Options opt = parse_args(argc, argv);
    const auto all_cases = cases();

    if (opt.list) {
        for (const Case& c : all_cases) {
            std::cout << c.name << " (" << c.adapter << ", " << c.asset << ")\n";
        }
        return static_cast<int>(Result::kPass);
    }

    const auto it = std::find_if(all_cases.begin(), all_cases.end(),
                                 [&](const Case& c) { return c.name == opt.case_name; });
    if (it == all_cases.end()) {
        std::cerr << "HARNESS_ERROR: unknown case '" << opt.case_name << "'\n";
        return static_cast<int>(Result::kHarnessError);
    }
    Case c = *it;
    if (opt.timeout_instructions != 0) c.timeout_instructions = opt.timeout_instructions;

    if (opt.assets_root.empty()) {
        std::cout << "SKIP: Z80_COMPAT_ASSETS is not set and --assets was not provided\n";
        return static_cast<int>(Result::kSkip);
    }

    const std::filesystem::path asset_path = std::filesystem::path(opt.assets_root) / c.asset;
    const std::vector<uint8_t> image = read_file(asset_path);
    if (image.empty()) {
        std::cout << "SKIP: asset not found or empty: " << asset_path << "\n";
        return static_cast<int>(Result::kSkip);
    }

    RunReport report;
    if (c.adapter == "cpm_com") {
        report = run_cpm_com(c, image);
    } else {
        std::cerr << "HARNESS_ERROR: unsupported adapter '" << c.adapter << "'\n";
        return static_cast<int>(Result::kHarnessError);
    }

    const std::filesystem::path report_path =
        std::filesystem::path(opt.artifact_root) / (c.name + ".log");
    write_text_file(report_path, report_text(c, asset_path, report));

    const char* label = report.result == Result::kPass ? "PASS" :
                        report.result == Result::kFail ? "FAIL" :
                        report.result == Result::kSkip ? "SKIP" : "HARNESS_ERROR";
    std::cout << label << ": " << c.name << " - " << report.reason << "\n"
              << "  log: " << report_path << "\n";
    return static_cast<int>(report.result);
}
