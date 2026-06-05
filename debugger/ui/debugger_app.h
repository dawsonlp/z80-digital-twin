//
// Z80 Digital Twin Debugger - DebuggerApp (ImGui front-end)
// Copyright (c) 2025 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//
// The ImGui application shell. It owns the CPU + DebugSession + symbol table,
// runs the GLFW/OpenGL frame loop, dispatches debug commands, and renders the
// panels. All execution control goes through DebugSession; panels only read
// CPU state and issue commands.
//

#ifndef Z80_DBG_DEBUGGER_APP_H
#define Z80_DBG_DEBUGGER_APP_H

#include "debug_session.h"
#include "disassembler.h"
#include "symbol_table.h"

#include <cstdint>
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

    /// @brief Run the GUI. In smoke mode, render a few frames and exit (used to
    ///        validate the toolchain/build without a human at the keyboard).
    /// @param shot_path If non-empty, render offscreen and write a PPM
    ///        screenshot here on the final frame, then exit (implies smoke).
    /// @return process exit code (0 on success).
    int Run(bool smoke = false, int smoke_frames = 3,
            const std::string& shot_path = {});

private:
    // -- Frame + command handling -------------------------------------------
    void DrawUi();
    void ExecuteCommands();         // applies pending one-shot commands
    void BeforeExecAction();        // clears dirty set ahead of a step/run

    // -- Panels --------------------------------------------------------------
    void DrawMenuBar();
    void DrawControlBar();
    void DrawRegisters();
    void DrawDisassembly();
    void DrawMemory();
    void DrawIoPorts();

    // -- Helpers -------------------------------------------------------------
    ByteReader Reader() const;
    SymbolResolver Resolver() const;

    // -- State ---------------------------------------------------------------
    DebugCPU cpu_;
    DebugSession session_{cpu_};
    SymbolTable symbols_;
    Disassembler disasm_;

    GLFWwindow* window_ = nullptr;

    // Pending one-shot commands set by the control bar, applied once per frame.
    bool cmd_step_ = false;
    bool cmd_step_over_ = false;
    bool cmd_run_ = false;
    bool cmd_pause_ = false;
    bool cmd_reset_ = false;

    // Per-frame run budget (instructions) when free-running.
    uint64_t run_budget_ = 250000;

    // Disassembly view.
    bool follow_pc_ = true;
    uint16_t disasm_top_ = 0x0000;

    // Memory view.
    char mem_goto_buf_[8] = "0000";
    bool mem_goto_pending_ = false;
    uint16_t mem_goto_addr_ = 0x0000;

    // Symbol load field.
    char sym_path_buf_[512] = "";

    // Status of the most recent action (for the status bar).
    std::string last_status_ = "Ready";
};

} // namespace z80::dbg

#endif // Z80_DBG_DEBUGGER_APP_H
