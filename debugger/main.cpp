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

namespace {

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
        "  --demo gcd|smc       Built-in demo when no program is given (default gcd).\n"
        "  --spectrum ROM       Boot ROM as a ZX Spectrum (adds screen + keyboard).\n"
        "  --tape FILE          Tape image (.tap/.tzx) for Spectrum mode; LOAD\"\"+F5.\n"
        "  --writable-rom       Allow writes to Spectrum ROM (off by default).\n"
        "  --bp HEX             Set a breakpoint at HEX address (repeatable).\n"
        "  --run N              Run N instructions (or N PAL frames in Spectrum\n"
        "                       mode) at startup — e.g. to populate state for a shot.\n"
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
    std::string spectrum_rom;
    std::string tape_path;
    bool writable_rom = false;
    uint16_t org = 0x0000;
    bool smoke = false;
    std::string shot_path;
    std::vector<uint16_t> breakpoints;
    std::string demo = "gcd";
    uint64_t run_count = 0;

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
            org = static_cast<uint16_t>(std::strtoul(argv[++i], nullptr, 0));
        } else if (arg == "--bp" && i + 1 < argc) {
            breakpoints.push_back(static_cast<uint16_t>(std::strtoul(argv[++i], nullptr, 16)));
        } else if (arg == "--demo" && i + 1 < argc) {
            demo = argv[++i];
        } else if (arg == "--spectrum" && i + 1 < argc) {
            spectrum_rom = argv[++i];
        } else if (arg == "--tape" && i + 1 < argc) {
            tape_path = argv[++i];
        } else if (arg == "--writable-rom") {
            writable_rom = true;
        } else if (arg == "--run" && i + 1 < argc) {
            run_count = std::strtoull(argv[++i], nullptr, 10);
        } else if (!arg.empty() && arg[0] != '-') {
            program_path = arg;
        } else {
            std::cerr << "Unknown argument: " << arg << "\n";
        }
    }

    DebuggerApp app;
    if (!spectrum_rom.empty()) {
        if (!app.LoadSpectrumRom(spectrum_rom)) return 1;
    } else if (!program_path.empty()) {
        if (!app.LoadProgramFile(program_path, org)) return 1;
    } else if (demo == "smc") {
        app.LoadSmcDemo();
    } else {
        app.LoadDemo();
    }
    if (!symbol_path.empty()) {
        app.LoadSymbolFile(symbol_path);
    }
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

    return app.Run(smoke, 5, shot_path);
}
