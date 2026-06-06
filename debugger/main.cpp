//
// Z80 Digital Twin Debugger - entry point
// Copyright (c) 2025 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//
// Usage:
//   z80_debugger [program.bin] [--org 0xADDR] [--sym file.sym]
//                [--demo gcd|smc] [--run N] [--bp HEX] [--smoke] [--shot FILE]
//
// With no program, a built-in demo is loaded (--demo gcd, the default, or
// --demo smc for a self-modifying example). --run N executes N instructions at
// startup (handy for scripting / to populate coverage + SMC before a shot).
//

#include "debugger_app.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    using namespace z80::dbg;

    std::string program_path;
    std::string symbol_path;
    std::string spectrum_rom;
    std::string tape_path;
    uint16_t org = 0x0000;
    bool smoke = false;
    std::string shot_path;
    std::vector<uint16_t> breakpoints;
    std::string demo = "gcd";
    uint64_t run_count = 0;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--smoke") {
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
    for (uint16_t bp : breakpoints) {
        app.AddBreakpoint(bp);
    }
    if (run_count > 0) {
        if (!spectrum_rom.empty()) app.RunSpectrumFrames(run_count);
        else app.RunInstructions(run_count);
    }

    return app.Run(smoke, 5, shot_path);
}
