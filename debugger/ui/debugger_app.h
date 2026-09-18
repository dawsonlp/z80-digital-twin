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
#include "live_patch.h"
#include "disassembler.h"
#include "symbol_table.h"
#include "ui_context.h"
#include "panel.h"
#include "spectrum/spectrum_machine.h"
#include "spectrum/tape.h"
#include "spectrum/beeper.h"
#include "spectrum/program_launch.h"
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

    /// @brief Import a legacy .sym transactionally into the active analysis project.
    bool LoadSymbolFile(const std::string& path);
    bool OpenAnalysisFile(const std::string& path);

    /// @brief Load a small built-in demo program (GCD) when none is supplied.
    void LoadDemo();

    /// @brief Load a tiny self-modifying demo (a self-incrementing operand loop).
    void LoadSmcDemo();

    /// @brief Load a ≤16 KB ROM as a ZX Spectrum: wire the ULA to this CPU and
    ///        enter Spectrum mode (a screen panel appears; free-run drives 50 Hz
    ///        frames through the session so breakpoints still apply).
    bool LoadSpectrumRom(const std::string& path);

    bool LoadSpectrumProgram(const std::string& rom, const std::string& program,
                             const machine::spectrum::ProgramLaunch& launch,
                             const std::string& symbols = {});
    void StartRunning();
    void ShowLivePatch() noexcept { show_live_patch_ = true; }

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
    void DrawLivePatch();
    void EnsureLivePatch();
    void ForgetLivePatch();
    void ExecuteCommands();   // applies the commands panels posted last frame
    UiContext MakeContext();  // fresh per-frame context for the panels

    // -- Spectrum mode -------------------------------------------------------
    void DriveSpectrumFrame();      // one PAL frame via the session (breakpoint-aware)
    void ConfigureSpectrumRom(const std::vector<uint8_t>& rom);
    void PollSpectrumKeyboard();    // host keys -> ULA matrix (when ImGui isn't typing)
    void ResetSpectrum();           // cold boot: reload ROM, zero RAM, reset CPU+ULA, run
    void PumpAudio();               // drain this frame's beeper edges -> PCM -> device

    // -- State ---------------------------------------------------------------
    std::unique_ptr<DebugCPU> generic_cpu_ = std::make_unique<DebugCPU>();
    std::unique_ptr<machine::spectrum::DebugSpectrumMachine> spectrum_;
    std::unique_ptr<DebugSession> session_ = std::make_unique<DebugSession>(*generic_cpu_);
    analysis::Workspace analysis_;
    std::unique_ptr<LivePatch> live_patch_;
    std::optional<LivePatch::Plan> patch_plan_;
    std::optional<analysis::Workspace> patch_analysis_;
    bool show_live_patch_ = false;
    std::string patch_path_;
    std::string patch_hash_;
    uint16_t patch_origin_ = 0x8000;
    bool patch_pc_enabled_ = false, patch_hl_enabled_ = false;
    uint16_t patch_pc_ = 0x8000, patch_hl_ = 0;
    bool patch_stack_enabled_ = false;
    uint16_t patch_stack_address_ = 0, patch_stack_value_ = 0;
    Disassembler disasm_;

    GLFWwindow* window_ = nullptr;

    DebugCommands commands_;
    std::string status_ = "Ready";
    std::optional<uint16_t> disasm_goto_;   ///< cross-panel "jump disassembly" request
    std::vector<std::unique_ptr<Panel>> panels_;

    uint64_t run_budget_ = 250000;   // instructions per frame while free-running
    std::string sym_path_;
    std::string analysis_path_;

    // Spectrum machine (active only after LoadSpectrumRom).
    std::vector<uint8_t> rom_image_;   ///< the loaded ROM, for cold-boot reset
    bool tape_play_prev_ = false;    ///< F5 edge detection
    bool spectrum_mode_ = false;     ///< driving a Spectrum (screen panel + frame run)
    bool spectrum_running_ = false;  ///< free-running the machine at 50 Hz

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
