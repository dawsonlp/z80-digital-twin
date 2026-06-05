//
// Z80 Digital Twin Debugger - DebuggerApp (ImGui front-end)
// Copyright (c) 2025 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//
// The application shell: owns the CPU + DebugSession + symbol table + the panel
// list, runs the GLFW/OpenGL frame loop, applies the commands panels post, and
// handles program/symbol loading and the menu bar. Per-panel rendering lives in
// the Panel classes under ui/panels/.
//

#ifndef Z80_DBG_DEBUGGER_APP_H
#define Z80_DBG_DEBUGGER_APP_H

#include "debug_session.h"
#include "disassembler.h"
#include "symbol_table.h"
#include "ui_context.h"
#include "panel.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct GLFWwindow;  // forward-declared; GLFW headers stay in the .cpp

namespace z80::dbg {

class DebuggerApp {
public:
    DebuggerApp();

    /// @brief Load a raw binary program at start_address (default 0x0000).
    bool LoadProgramFile(const std::string& path, uint16_t start_address = 0x0000);

    /// @brief Load a .sym symbol file (merges; non-fatal on error).
    bool LoadSymbolFile(const std::string& path);

    /// @brief Load a small built-in demo program (GCD) when none is supplied.
    void LoadDemo();

    /// @brief Set a breakpoint at an address (e.g. from the command line).
    void AddBreakpoint(uint16_t address);

    /// @brief Run the GUI. In smoke mode, render a few frames and exit.
    /// @param shot_path If non-empty, write a PPM screenshot on the final frame.
    int Run(bool smoke = false, int smoke_frames = 3,
            const std::string& shot_path = {});

private:
    void DrawMenuBar();
    void ExecuteCommands();   // applies the commands panels posted last frame
    UiContext MakeContext();  // fresh per-frame context for the panels

    // -- State ---------------------------------------------------------------
    DebugCPU cpu_;
    DebugSession session_{cpu_};
    SymbolTable symbols_;
    Disassembler disasm_;

    GLFWwindow* window_ = nullptr;

    DebugCommands commands_;
    std::string status_ = "Ready";
    std::vector<std::unique_ptr<Panel>> panels_;

    uint64_t run_budget_ = 250000;   // instructions per frame while free-running
    char sym_path_buf_[512] = "";    // menu: symbol-file path field
};

} // namespace z80::dbg

#endif // Z80_DBG_DEBUGGER_APP_H
