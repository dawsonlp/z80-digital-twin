//
// Z80 Digital Twin Debugger - DebuggerApp (ImGui front-end)
// Copyright (c) 2025-2026 Larry Dawson
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
#include "spectrum/ula.h"
#include "spectrum/tape.h"
#include "spectrum/beeper.h"
#include "audio_output.h"

#include <chrono>

#include <cstdint>
#include <memory>
#include <optional>
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

    /// @brief Load a tiny self-modifying demo (a self-incrementing operand loop).
    void LoadSmcDemo();

    /// @brief Load a ≤16 KB ROM as a ZX Spectrum: wire the ULA to this CPU and
    ///        enter Spectrum mode (a screen panel appears; free-run drives 50 Hz
    ///        frames through the session so breakpoints still apply).
    bool LoadSpectrumRom(const std::string& path);

    /// @brief Load a `.tap` for the Spectrum (press F5 in the window to play).
    bool LoadTape(const std::string& path);

    /// @brief Write-protect the ROM (0x0000–0x3FFF). Off by default so the SMC
    ///        panel can flag stray ROM writes during diagnosis.
    void SetRomWriteProtect(bool on);

    /// @brief Set a breakpoint at an address (e.g. from the command line).
    void AddBreakpoint(uint16_t address);

    /// @brief Execute up to @p count instructions immediately (CLI/scripting).
    void RunInstructions(uint64_t count);

    /// @brief Drive @p count PAL frames immediately (Spectrum mode; CLI/scripting,
    ///        e.g. to boot before a screenshot). Stops early on a breakpoint.
    void RunSpectrumFrames(uint64_t count);

    /// @brief Run the GUI. In smoke mode, render a few frames and exit.
    /// @param shot_path If non-empty, write a PPM screenshot on the final frame.
    int Run(bool smoke = false, int smoke_frames = 3,
            const std::string& shot_path = {});

private:
    void DrawMenuBar();
    void ExecuteCommands();   // applies the commands panels posted last frame
    UiContext MakeContext();  // fresh per-frame context for the panels

    // -- Spectrum mode -------------------------------------------------------
    void DriveSpectrumFrame();      // one PAL frame via the session (breakpoint-aware)
    void PollSpectrumKeyboard();    // host keys -> ULA matrix (when ImGui isn't typing)
    void ResetSpectrum();           // cold boot: reload ROM, zero RAM, reset CPU+ULA, run
    void PumpAudio();               // drain this frame's beeper edges -> PCM -> device

    // -- State ---------------------------------------------------------------
    DebugCPU cpu_;
    DebugSession session_{cpu_};
    SymbolTable symbols_;
    Disassembler disasm_;

    GLFWwindow* window_ = nullptr;

    DebugCommands commands_;
    std::string status_ = "Ready";
    std::optional<uint16_t> disasm_goto_;   ///< cross-panel "jump disassembly" request
    std::vector<std::unique_ptr<Panel>> panels_;

    uint64_t run_budget_ = 250000;   // instructions per frame while free-running
    char sym_path_buf_[512] = "";    // menu: symbol-file path field

    // Spectrum machine (active only after LoadSpectrumRom).
    machine::spectrum::Ula ula_;
    machine::spectrum::Tape tape_;
    std::vector<uint8_t> rom_image_;   ///< the loaded ROM, for cold-boot reset
    bool tape_play_prev_ = false;    ///< F5 edge detection
    bool spectrum_mode_ = false;     ///< driving a Spectrum (screen panel + frame run)
    bool spectrum_running_ = false;  ///< free-running the machine at 50 Hz
    bool frame_active_ = false;      ///< mid-frame (a breakpoint may have paused us)
    uint64_t frame_budget_ = 0;      ///< T-states left in the current frame

    // Audio (beeper). 50 Hz wall-clock pacing keeps sample production ≈ 44.1 kHz.
    static constexpr uint32_t kAudioRate = 44100;
    audio::AudioOutput audio_;
    machine::spectrum::BeeperResampler beeper_{machine::spectrum::timing::kCpuHz, kAudioRate};
    std::vector<int16_t> audio_samples_;
    bool sound_ = false;
    std::chrono::steady_clock::time_point last_frame_time_;
    double frame_accum_ = 0.0;       ///< wall-clock time owed in 50 Hz frames
    bool paced_ = false;             ///< pacing clock initialised
};

} // namespace z80::dbg

#endif // Z80_DBG_DEBUGGER_APP_H
