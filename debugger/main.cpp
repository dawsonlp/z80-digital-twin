//
// Z80 Digital Twin Debugger - entry point
// Copyright (c) 2025-2026 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//
// Usage:
//   z80_debugger [program.bin] [--org 0xADDR] [--sym file.sym] [--demo gcd|smc]
//                [--spectrum ROM] [--tape FILE] [--writable-rom] [--run N]
//                [--bp HEX] [--smoke] [--shot FILE] [-h|--help]
//
// With no program, a built-in demo is loaded (--demo gcd, the default, or
// --demo smc for a self-modifying example). --run N executes N instructions at
// startup (handy for scripting / to populate coverage + SMC before a shot).
// Run with --help for the full option list.
//

#include "debugger_app.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>
#include <optional>
#include <stdexcept>

namespace {

uint64_t number(const std::string& text, uint64_t maximum, int base = 0) {
    std::size_t used = 0;
    if (text.empty() || text.front() == '-' || text.front() == '+')
        throw std::invalid_argument("invalid unsigned number: " + text);
    const auto n = std::stoull(text, &used, base);
    if (used != text.size() || n > maximum)
        throw std::invalid_argument("number out of range: " + text);
    return n;
}

void print_usage(const char* prog) {
    std::cout <<
        "Z80 Digital Twin Debugger — ImGui front-end for the Z80 CPU / ZX Spectrum\n"
        "\n"
        "Opens a windowed debugger (registers, disassembly, memory, I/O, SMC, and —\n"
        "in Spectrum mode — a live screen + keyboard). With no program a built-in\n"
        "demo is loaded.\n"
        "\n"
        "Usage:\n"
        "  " << prog << " [program.bin] [options]\n"
        "\n"
        "Arguments:\n"
        "  program.bin          Raw Z80 binary to load at --org (default 0x0000).\n"
        "\n"
        "Options:\n"
        "  --org 0xADDR         Load address for program.bin (default 0x0000).\n"
        "  --sym FILE           Load a .sym symbol file (address<->name map).\n"
        "  --analysis FILE      Open durable analysis for the exact loaded image.\n"
        "  --demo gcd|smc       Built-in demo when no program is given (default gcd).\n"
        "  --spectrum ROM       Boot ROM as a ZX Spectrum (adds screen + keyboard).\n"
        "  --entry ADDR         With Spectrum + binary: standalone program entry.\n"
        "  --sp ADDR            Initial stack pointer for standalone launch.\n"
        "  --stack-reserve N    Writable bytes below SP (default 256).\n"
        "  --start              Start running immediately (otherwise paused).\n"
        "  --tape FILE          Tape image (.tap/.tzx) for Spectrum mode; LOAD\"\"+F5.\n"
        "  --writable-rom       Allow writes to Spectrum ROM (off by default).\n"
        "  --bp HEX             Set a breakpoint at HEX address (repeatable).\n"
        "  --run N              Run N instructions (or N PAL frames in Spectrum\n"
        "                       mode) at startup — e.g. to populate state for a shot.\n"
        "  --steps N            Step N instructions in any mode, then pause (after --run).\n"
        "  --shot FILE          Write a PPM screenshot on the final frame.\n"
        "  --smoke              Render a few frames headless and exit (CI smoke test).\n"
        "  -h, --help           Show this help and exit.\n"
        "\n"
        "Examples:\n"
        "  " << prog << " program.bin --org 0x8000 --sym program.sym\n"
        "  " << prog << " --demo smc\n"
        "  " << prog << " --spectrum spec48.rom --tape \"Jetpac.tzx\"\n";
}

} // namespace

int main(int argc, char** argv) {
    using namespace z80::dbg;

    std::string program_path;
    std::string symbol_path;
    std::string analysis_path;
    std::string spectrum_rom;
    std::string tape_path;
    bool writable_rom = false;
    uint16_t org = 0x0000;
    bool smoke = false;
    std::string shot_path;
    std::vector<uint16_t> breakpoints;
    std::string demo = "gcd";
    uint64_t run_count = 0;
    uint64_t step_count = 0;
    std::optional<uint32_t> entry, stack;
    uint32_t stack_reserve = 256;
    bool start = false;
    bool reserve_set = false;

    try {
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "-h" || arg == "--help") {
                print_usage(argv[0]);
                return 0;
            } else if (arg == "--smoke") {
                smoke = true;
            } else if (arg == "--shot" && i + 1 < argc) {
                shot_path = argv[++i];
            } else if (arg == "--sym" && i + 1 < argc) {
                symbol_path = argv[++i];
            } else if (arg == "--org" && i + 1 < argc) {
                org = static_cast<uint16_t>(number(argv[++i], 0xFFFF));
            } else if (arg == "--entry" && i + 1 < argc) {
                entry = static_cast<uint32_t>(number(argv[++i], 0xFFFF));
            } else if (arg == "--sp" && i + 1 < argc) {
                stack = static_cast<uint32_t>(number(argv[++i], 0xFFFF));
            } else if (arg == "--stack-reserve" && i + 1 < argc) {
                stack_reserve = static_cast<uint32_t>(number(argv[++i], 0xFFFF));
                reserve_set = true;
            } else if (arg == "--start") {
                start = true;
            } else if (arg == "--bp" && i + 1 < argc) {
                breakpoints.push_back(static_cast<uint16_t>(number(argv[++i], 0xFFFF, 16)));
            } else if (arg == "--analysis" && i + 1 < argc) {
                analysis_path = argv[++i];
            } else if (arg == "--demo" && i + 1 < argc) {
                demo = argv[++i];
            } else if (arg == "--spectrum" && i + 1 < argc) {
                spectrum_rom = argv[++i];
            } else if (arg == "--tape" && i + 1 < argc) {
                tape_path = argv[++i];
            } else if (arg == "--writable-rom") {
                writable_rom = true;
            } else if (arg == "--steps" && i + 1 < argc) {
                step_count = number(argv[++i], UINT64_MAX, 10);
            } else if (arg == "--run" && i + 1 < argc) {
                run_count = number(argv[++i], UINT64_MAX, 10);
            } else if (!arg.empty() && arg[0] != '-') {
                if (!program_path.empty()) throw std::invalid_argument("multiple program files");
                program_path = arg;
            } else {
                throw std::invalid_argument("unknown argument or missing value: " + arg);
            }
        }

        if (!analysis_path.empty() && !symbol_path.empty())
            throw std::invalid_argument("choose --analysis or --sym for initial annotations");

        const bool standalone = !spectrum_rom.empty() && !program_path.empty();
        if (standalone && (!entry || !stack || !tape_path.empty() || writable_rom))
            throw std::invalid_argument("Spectrum binary launch requires --entry and --sp; tape and writable ROM are incompatible");
        if (!standalone && (entry || stack || reserve_set))
            throw std::invalid_argument("--entry/--sp/--stack-reserve require Spectrum + binary");

        DebuggerApp app;
        if (standalone) {
            if (!app.LoadSpectrumProgram(spectrum_rom, program_path,
                    {org, *entry, *stack, stack_reserve}, symbol_path)) return 1;
        } else if (!spectrum_rom.empty()) {
            if (!app.LoadSpectrumRom(spectrum_rom)) return 1;
        } else if (!program_path.empty()) {
            if (!app.LoadProgramFile(program_path, org)) return 1;
        } else if (demo == "smc") {
            app.LoadSmcDemo();
        } else {
            app.LoadDemo();
        }
        if (!standalone && !symbol_path.empty()) {
            app.LoadSymbolFile(symbol_path);
        }
        if (!analysis_path.empty() && !app.OpenAnalysisFile(analysis_path)) return 1;
        if (!tape_path.empty() && !spectrum_rom.empty()) {
            app.LoadTape(tape_path);
        }
        if (writable_rom && !spectrum_rom.empty()) {
            app.SetRomWriteProtect(false);   // ROM is protected by default
        }
        for (uint16_t bp : breakpoints) {
            app.AddBreakpoint(bp);
        }
        if (run_count > 0) {
            if (!spectrum_rom.empty()) app.RunSpectrumFrames(run_count);
            else app.RunInstructions(run_count);
        }
        if (step_count > 0) app.RunInstructions(step_count);
        if (start) app.StartRunning();

        return app.Run(smoke, 5, shot_path);
    } catch (const std::exception& e) {
        std::cerr << "Arguments: " << e.what() << '\n';
        return 1;
    }
}
