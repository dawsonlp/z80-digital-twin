//
// Z80 Digital Twin Debugger - entry point
// Copyright (c) 2025 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//
// Usage:
//   z80_debugger [program.bin] [--org 0xADDR] [--sym file.sym] [--smoke]
//
// With no program, a built-in GCD demo is loaded so the UI has something to
// step through. --smoke renders a few frames headlessly and exits (build check).
//

#include "debugger_app.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    using namespace z80::dbg;

    std::string program_path;
    std::string symbol_path;
    uint16_t org = 0x0000;
    bool smoke = false;
    std::string shot_path;

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
        } else if (!arg.empty() && arg[0] != '-') {
            program_path = arg;
        } else {
            std::cerr << "Unknown argument: " << arg << "\n";
        }
    }

    DebuggerApp app;
    if (program_path.empty()) {
        app.LoadDemo();
    } else if (!app.LoadProgramFile(program_path, org)) {
        return 1;
    }
    if (!symbol_path.empty()) {
        app.LoadSymbolFile(symbol_path);
    }

    return app.Run(smoke, 5, shot_path);
}
