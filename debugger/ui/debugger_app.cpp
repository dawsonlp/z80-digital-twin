//
// Z80 Digital Twin Debugger - DebuggerApp implementation
// Copyright (c) 2025-2026 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//

#include "debugger_app.h"

#include "panels/control_panel.h"
#include "panels/registers_panel.h"
#include "panels/disassembly_panel.h"
#include "panels/memory_panel.h"
#include "panels/io_panel.h"
#include "panels/smc_panel.h"
#include "panels/screen_panel.h"
#include "panels/keyboard_panel.h"

#include "spectrum/timing.h"
#include "spectrum/keyboard.h"

#define GL_SILENCE_DEPRECATION
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>
#include "portable-file-dialogs.h"

#include <chrono>
#include <cstdint>
#include <fstream>
#include <format>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace z80::dbg {
namespace {

const char* reason_text(StopReason r) {
    switch (r) {
        case StopReason::StepComplete:    return "step complete";
        case StopReason::Breakpoint:      return "breakpoint";
        case StopReason::Watchpoint:      return "watchpoint";
        case StopReason::Halted:          return "halted";
        case StopReason::BudgetExhausted: return "running";
        case StopReason::IncompleteInstruction: return "instruction incomplete (prefix budget)";
        case StopReason::AlreadyHalted:   return "already halted";
        case StopReason::SelfModified:    return "self-modifying code!";
    }
    return "?";
}

void glfw_error_callback(int error, const char* description) {
    std::cerr << "GLFW error " << error << ": " << description << "\n";
}

} // namespace

DebuggerApp::DebuggerApp() {
    panels_.push_back(std::make_unique<ControlPanel>());
    panels_.push_back(std::make_unique<RegistersPanel>());
    panels_.push_back(std::make_unique<DisassemblyPanel>());
    panels_.push_back(std::make_unique<MemoryPanel>());
    panels_.push_back(std::make_unique<IoPanel>());
    panels_.push_back(std::make_unique<SmcPanel>());
}

UiContext DebuggerApp::MakeContext() {
    return UiContext{*session_, symbols_, disasm_, commands_, status_, disasm_goto_};
}

bool DebuggerApp::LoadProgramFile(const std::string& path, uint16_t start_address) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::cerr << "Could not open program: " << path << "\n";
        return false;
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
    session_->Cpu().Reset();
    session_->Cpu().LoadProgram(bytes, start_address);
    session_->ClearDirty();   // program load isn't a "change" to highlight
    status_ = std::format("Loaded {} bytes from {}", bytes.size(), path);
    return true;
}

bool DebuggerApp::LoadSymbolFile(const std::string& path) {
    std::vector<std::string> warnings;
    const bool ok = symbols_.LoadFromFile(path, nullptr, &warnings);
    for (const auto& w : warnings) std::cerr << "symbols: " << w << "\n";
    if (ok) status_ = std::format("Loaded {} symbols from {}", symbols_.Size(), path);
    return ok;
}

void DebuggerApp::LoadDemo() {
    // GCD by repeated subtraction; result stored to RESULT (0x9000) then HALT.
    const std::vector<uint8_t> program = {
        0x7A,             // 0x0000 LD A, D
        0xB3,             // 0x0001 OR E
        0x28, 0x0B,       // 0x0002 JR Z, 0x000F   -> DONE
        0xB7,             // 0x0004 OR A
        0xED, 0x52,       // 0x0005 SBC HL, DE
        0x30, 0x02,       // 0x0007 JR NC, 0x000B  -> NO_SWAP
        0x19,             // 0x0009 ADD HL, DE
        0xEB,             // 0x000A EX DE, HL
        0x18, 0xF3,       // 0x000B JR 0x0000      -> GCD_LOOP
        0x18, 0xF1,       // 0x000D JR 0x0000
        0x22, 0x00, 0x90, // 0x000F LD (0x9000), HL -> RESULT  (DONE)
        0x76,             // 0x0012 HALT
    };
    session_->Cpu().Reset();
    session_->Cpu().LoadProgram(program, 0x0000);
    session_->Cpu().HL() = 1071;     // GCD(1071, 462) = 21
    session_->Cpu().DE() = 462;
    session_->ClearDirty();   // program load isn't a "change" to highlight

    symbols_.DefineLabel(0x0000, "GCD_LOOP", SymbolType::Function,   "GCD by subtraction");
    symbols_.DefineLabel(0x000B, "NO_SWAP",  SymbolType::JumpTarget, "HL >= DE: loop without swap");
    symbols_.DefineLabel(0x000F, "DONE",     SymbolType::Label,      "store result and halt");
    symbols_.DefineLabel(0x9000, "RESULT",   SymbolType::WordVariable, "GCD result (16-bit)");
    status_ = "Loaded built-in GCD demo (HL=1071, DE=462)";
}

void DebuggerApp::LoadSmcDemo() {
    // Self-incrementing operand loop — modifies its own LD A,n operand each pass.
    //   0x0000 3E 00     LD A, 0x00      (operand at 0x0001 is rewritten)
    //   0x0002 21 01 00  LD HL, 0x0001
    //   0x0005 34        INC (HL)        -> writes 0x0001 (executed code = SMC)
    //   0x0006 18 F8     JR 0x0000
    const std::vector<uint8_t> program = {
        0x3E, 0x00, 0x21, 0x01, 0x00, 0x34, 0x18, 0xF8};
    session_->Cpu().Reset();
    session_->Reset();
    session_->Cpu().LoadProgram(program, 0x0000);
    session_->ClearDirty();

    symbols_.DefineLabel(0x0000, "LOOP",    SymbolType::Function,     "self-modifying loop");
    symbols_.DefineLabel(0x0001, "COUNTER", SymbolType::ByteVariable, "operand patched each pass");
    status_ = "Loaded self-modifying demo (INC (HL) rewrites its own operand)";
}

bool DebuggerApp::LoadSpectrumRom(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::cerr << "Could not open ROM: " << path << "\n";
        return false;
    }
    std::vector<uint8_t> rom((std::istreambuf_iterator<char>(in)),
                             std::istreambuf_iterator<char>());
    if (rom.empty() || rom.size() > 0x4000) {
        std::cerr << "ROM must be 1..16384 bytes: " << path << "\n";
        return false;
    }

    ConfigureSpectrumRom(rom);
    return true;
}

void DebuggerApp::ConfigureSpectrumRom(const std::vector<uint8_t>& rom) {
    if (!spectrum_) {
        session_.reset();
        spectrum_ = std::make_unique<machine::spectrum::DebugSpectrumMachine>();
        session_ = std::make_unique<DebugSession>(spectrum_->cpu());
        generic_cpu_.reset();
        session_->SetExecutionHooks([this] { spectrum_->prepare_execution(); },
                                    [this] { spectrum_->advance_execution(); });
        spectrum_->on_frame_completed([this] { PumpAudio(); });
        panels_.push_back(std::make_unique<SpectrumScreenPanel>(*spectrum_));
        panels_.push_back(std::make_unique<KeyboardPanel>(spectrum_->ula()));
    }
    spectrum_->load_rom(rom);
    session_->Reset();
    rom_image_ = rom;
    spectrum_->set_rom_write_protect(true);

    spectrum_mode_ = true;
    spectrum_running_ = false;

    session_->ClearDirty();
    status_ = std::format("Loaded ZX Spectrum ROM ({} bytes) — press Run", rom.size());
}

bool DebuggerApp::LoadSpectrumProgram(const std::string& rom_path,
        const std::string& program_path, const machine::spectrum::ProgramLaunch& launch,
        const std::string& symbol_path) {
    try {
        if (spectrum_mode_) throw std::invalid_argument("program launch requires a fresh machine");
        auto read = [](const std::string& path) {
            std::ifstream in(path, std::ios::binary);
            if (!in) throw std::runtime_error("could not open " + path);
            return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), {});
        };
        const auto rom = read(rom_path);
        const auto program = read(program_path);
        if (rom.size() != 0x4000)
            throw std::invalid_argument("standalone launch requires an exact 16384-byte ROM");
        machine::spectrum::ValidateProgramLaunch(program.size(), launch);
        SymbolTable symbols;
        if (!symbol_path.empty()) {
            std::vector<std::string> warnings;
            if (!symbols.LoadFromFile(symbol_path, nullptr, &warnings) || !warnings.empty())
                throw std::invalid_argument("invalid debugger symbol file: " + symbol_path);
        }
        // All input errors have been checked before setting up the machine.
        ConfigureSpectrumRom(rom);
        session_->Reset();
        machine::spectrum::LoadRamProgram(session_->Cpu(), program, launch);
        symbols_ = std::move(symbols);
        disasm_goto_ = session_->Cpu().PC();
        status_ = std::format("Spectrum program: {} bytes @ ${:04X}, entry ${:04X}",
                              program.size(), launch.origin, launch.entry);
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Spectrum launch: " << e.what() << '\n';
        return false;
    }
}

void DebuggerApp::StartRunning() {
    if (spectrum_mode_) spectrum_running_ = true;
    else session_->Run();
}

bool DebuggerApp::LoadTape(const std::string& path) {
    if (!spectrum_) return false;
    std::ifstream in(path, std::ios::binary);
    if (!in) { std::cerr << "Could not open tape: " << path << "\n"; return false; }
    std::vector<uint8_t> tap((std::istreambuf_iterator<char>(in)),
                             std::istreambuf_iterator<char>());
    if (tap.empty() || !spectrum_->tape().load(tap)) {
        std::cerr << "Failed to parse tape: " << path << "\n";
        return false;
    }
    status_ = std::format("Tape: {} ({} blocks) — type LOAD\"\" then press F5",
                          path, spectrum_->tape().block_count());
    return true;
}

void DebuggerApp::SetRomWriteProtect(bool on) {
    if (on) session_->Cpu().GetMemory().SetWriteProtect(0x0000, 0x3FFF);
    else session_->Cpu().GetMemory().ClearWriteProtect();
    status_ = on ? "ROM write-protected (0x0000-0x3FFF)" : "ROM writable";
}

void DebuggerApp::AddBreakpoint(uint16_t address) {
    session_->AddBreakpoint(address);
}

void DebuggerApp::RunSpectrumFrames(uint64_t count) {
    if (!spectrum_mode_) return;
    session_->Run();
    spectrum_running_ = true;
    for (uint64_t i = 0; i < count && spectrum_running_; ++i) DriveSpectrumFrame();
}

void DebuggerApp::ResetSpectrum() {
    // Cold boot — as if freshly started: reload the ROM image, zero RAM, reset
    // the CPU and ULA, and run. RawWrite bypasses observers and write-protection.
    auto& mem = session_->Cpu().GetMemory();
    for (std::size_t i = 0; i < rom_image_.size() && i < 0x4000; ++i)
        mem.RawWrite(static_cast<uint16_t>(i), rom_image_[i]);
    for (uint32_t a = 0x4000; a <= 0xFFFF; ++a)
        mem.RawWrite(static_cast<uint16_t>(a), 0x00);

    spectrum_->reset_timing();
    session_->Reset();            // cpu.Reset() + clear coverage/SMC/blocked/dirty
    spectrum_running_ = true;     // re-boot and run
    status_ = "Reset (cold boot)";
}

void DebuggerApp::PumpAudio() {
    if (!sound_) return;
    audio_samples_.clear();
    for (const auto& e : spectrum_->ula().beeper_edges())
        beeper_.edge(e.cycle, e.level, audio_samples_);
    beeper_.advance(session_->Cpu().GetCycleCount(), audio_samples_);
    audio_.push(audio_samples_);
}

void DebuggerApp::DriveSpectrumFrame() {
    const auto frame = spectrum_->frame_count();
    while (spectrum_->frame_count() == frame) {
        const StepResult r = session_->RunSlice(1);
        if (r.reason == StopReason::Breakpoint || r.reason == StopReason::Watchpoint ||
            r.reason == StopReason::SelfModified || r.reason == StopReason::IncompleteInstruction) {
            spectrum_running_ = false;
            status_ = std::format("Spectrum stopped: {} @ 0x{:04X}", reason_text(r.reason), r.pc);
            break;
        }
        if (r.reason == StopReason::Halted && spectrum_->frame_count() == frame) {
            spectrum_->advance_execution();
        }
    }
}

void DebuggerApp::PollSpectrumKeyboard() {
    if (!spectrum_mode_ || ImGui::GetIO().WantCaptureKeyboard) return;
    namespace kb = machine::spectrum::keyboard;

    spectrum_->ula().release_all_keys();
    const auto down = [this](int key) { return glfwGetKey(window_, key) == GLFW_PRESS; };
    const auto press = [this](kb::Key k) { spectrum_->ula().key_down(k.half_row, k.bit); };

    for (const kb::AsciiKey& k : kb::kAsciiKeys)
        if (down(k.c)) spectrum_->ula().key_down(k.half_row, k.bit);
    if (down(GLFW_KEY_ENTER)) press(kb::kEnter);
    if (down(GLFW_KEY_SPACE)) press(kb::kSpace);
    if (down(GLFW_KEY_LEFT_SHIFT) || down(GLFW_KEY_RIGHT_SHIFT)) press(kb::kCapsShift);
    if (down(GLFW_KEY_LEFT_CONTROL) || down(GLFW_KEY_RIGHT_CONTROL)) press(kb::kSymbolShift);
    if (down(GLFW_KEY_BACKSPACE)) { press(kb::kCapsShift); press(kb::key_for_ascii('0')); }
}

void DebuggerApp::RunInstructions(uint64_t count) {
    spectrum_running_ = false;
    for (uint64_t i = 0; i < count; ++i) {
        const StepResult r = session_->StepInstruction();
        if (r.reason == StopReason::Halted || r.reason == StopReason::AlreadyHalted ||
            r.reason == StopReason::IncompleteInstruction) break;
    }
}

void DebuggerApp::ExecuteCommands() {
    if (commands_.reset) {
        if (spectrum_mode_) {
            ResetSpectrum();     // cold boot the machine
        } else {
            session_->Reset();
            status_ = "Reset";
        }
    }
    if (commands_.step) {
        spectrum_running_ = false;
        session_->ClearDirty();
        const StepResult r = session_->StepInstruction();
        status_ = std::format("Step: {} @ 0x{:04X} (+{} T)",
                              reason_text(r.reason), r.pc, r.cycles);
    }
    if (commands_.step_over) {
        spectrum_running_ = false;
        session_->ClearDirty();
        const StepResult r = session_->StepOver();
        status_ = std::format("Step over: {} @ 0x{:04X} (+{} T)",
                              reason_text(r.reason), r.pc, r.cycles);
    }
    if (commands_.run) {
        session_->ClearDirty();
        session_->Run();
        if (spectrum_mode_) { spectrum_running_ = true; status_ = "Spectrum running (50 Hz)"; }
        else status_ = "Running...";
    }
    if (commands_.pause) {
        session_->Pause();
        spectrum_running_ = false;
        status_ = "Paused";
    }
    commands_.Clear();

    if (spectrum_mode_) {
        // Free-run paced to 50 Hz wall-clock (so it runs at real speed and the
        // beeper feeds the sound card at ≈44.1 kHz), breakpoint-aware.
        if (spectrum_running_) {
            const auto now = std::chrono::steady_clock::now();
            if (!paced_) { last_frame_time_ = now; paced_ = true; }
            frame_accum_ += std::chrono::duration<double>(now - last_frame_time_).count();
            last_frame_time_ = now;
            if (frame_accum_ > 0.25) frame_accum_ = 0.25;   // don't burst after a stall
            const double period = 1.0 / machine::spectrum::timing::kFrameRateHz;
            int ran = 0;
            while (frame_accum_ >= period && ran < 4 && spectrum_running_) {
                DriveSpectrumFrame();
                frame_accum_ -= period;
                ++ran;
            }
        } else {
            paced_ = false;   // reset pacing while paused so resume doesn't catch up
        }
    } else if (session_->State() == RunState::Running) {
        // While running, advance a bounded slice per frame.
        const StepResult r = session_->RunSlice(run_budget_);
        if (session_->State() != RunState::Running) {
            status_ = std::format("Stopped: {} @ 0x{:04X}", reason_text(r.reason), r.pc);
        }
    }
}

void DebuggerApp::DrawMenuBar() {
    if (!ImGui::BeginMainMenuBar()) return;
    if (ImGui::BeginMenu("File")) {
        ImGui::TextDisabled("Load via CLI: z80_debugger <prog.bin> [--sym file.sym]");
        ImGui::Separator();
        ImGui::SetNextItemWidth(360);
        ImGui::InputTextWithHint("##sympath", "path to .sym", sym_path_buf_,
                                 sizeof(sym_path_buf_));
        ImGui::SameLine();
        if (ImGui::Button("Load Symbols") && sym_path_buf_[0] != '\0') {
            LoadSymbolFile(sym_path_buf_);
        }
        ImGui::SameLine();
        if (ImGui::Button("Save Symbols") && sym_path_buf_[0] != '\0') {
            if (symbols_.SaveToFile(sym_path_buf_))
                status_ = std::format("Saved {} symbols to {}", symbols_.Size(), sym_path_buf_);
            else
                status_ = std::format("Failed to save symbols to {}", sym_path_buf_);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Open tape…", nullptr, false, spectrum_mode_)) {
            auto sel = pfd::open_file("Open tape", ".",
                                      {"ZX Spectrum tapes (.tap .tzx)", "*.tap *.tzx",
                                       "All files", "*"}).result();
            if (!sel.empty()) LoadTape(sel.front());
        }
        if (!spectrum_mode_)
            ImGui::TextDisabled("(load a Spectrum ROM with --spectrum to use tapes)");
        ImGui::Separator();
        if (ImGui::MenuItem("Quit")) {
            glfwSetWindowShouldClose(window_, GLFW_TRUE);
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help")) {
        ImGui::TextDisabled("Z80 Digital Twin Debugger");
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
}

int DebuggerApp::Run(bool smoke, int smoke_frames, const std::string& shot_path) {
    if (!shot_path.empty()) {
        smoke = true;
        if (smoke_frames < 5) smoke_frames = 5;  // let the layout settle
    }
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW\n";
        return 1;
    }

#if defined(__APPLE__)
    const char* glsl_version = "#version 150";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#else
    const char* glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
#endif
    if (smoke) glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

    window_ = glfwCreateWindow(1600, 1000, "Z80 Digital Twin Debugger", nullptr, nullptr);
    if (!window_) {
        std::cerr << "Failed to create GLFW window (no display?)\n";
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window_);
    // Vsync OFF: the swap must not block, so the input/emulation loop below can
    // poll the keyboard and step the 50 Hz machine faster than the monitor
    // refreshes (low input latency). Rendering is throttled separately. Trade-off:
    // the picture can tear slightly without vsync — a worthwhile trade for a game.
    glfwSwapInterval(0);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    // In Spectrum mode the host keyboard drives the machine, so leave ImGui's
    // keyboard navigation off — otherwise it holds WantCaptureKeyboard and eats
    // every keystroke before it reaches the ULA. (Text fields still grab focus,
    // so typing a symbol path etc. is unaffected.)
    if (!spectrum_mode_) ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    if (smoke) ImGui::GetIO().IniFilename = nullptr;  // deterministic screenshots
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window_, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    // Beeper audio (Spectrum mode only; harmless if no device is available).
    if (spectrum_mode_ && !smoke) {
        sound_ = audio_.start(kAudioRate);
        std::cout << (sound_ ? "sound: on (beeper, 44100 Hz)\n"
                             : "sound: no audio device\n") << std::flush;
    }

    // The input/emulation loop runs much faster than the display (vsync is off):
    // it pumps OS events, samples the keyboard, and steps the 50 Hz machine every
    // iteration, but only *repaints* at kRenderHz. So a keypress is picked up
    // within ~1 ms (one poll), not up to a full monitor-refresh period.
    using clock = std::chrono::steady_clock;
    constexpr double kRenderHz = 60.0;   // repaint cap (content only changes at 50 Hz)
    const auto render_period = std::chrono::duration_cast<clock::duration>(
        std::chrono::duration<double>(1.0 / kRenderHz));
    auto last_render = clock::now();

    int frame = 0;
    while (!glfwWindowShouldClose(window_)) {
        glfwPollEvents();
        PollSpectrumKeyboard();
        if (spectrum_mode_) {   // F5 = play tape (key-down edge)
            const bool f5 = glfwGetKey(window_, GLFW_KEY_F5) == GLFW_PRESS;
            if (f5 && !tape_play_prev_) {
                spectrum_->tape().play(session_->Cpu().GetCycleCount());
                status_ = "Tape: playing";
            }
            tape_play_prev_ = f5;
        }
        ExecuteCommands();

        // Repaint only every render_period; otherwise idle ~0.5 ms so the loop
        // polls input at ~1-2 kHz instead of busy-spinning a core.
        if (!smoke && clock::now() - last_render < render_period) {
            std::this_thread::sleep_for(std::chrono::microseconds(500));
            continue;
        }
        last_render = clock::now();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        DrawMenuBar();
        UiContext ctx = MakeContext();
        for (auto& panel : panels_) panel->Draw(ctx);

        ImGui::Render();
        int w, h;
        glfwGetFramebufferSize(window_, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.10f, 0.10f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        const bool last_frame = smoke && (frame + 1 >= smoke_frames);
        if (last_frame && !shot_path.empty()) {
            std::vector<unsigned char> px(static_cast<size_t>(w) * h * 3);
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px.data());
            std::ofstream f(shot_path, std::ios::binary);
            if (f) {
                f << "P6\n" << w << " " << h << "\n255\n";
                for (int y = h - 1; y >= 0; --y)   // GL is bottom-up; flip
                    f.write(reinterpret_cast<const char*>(&px[static_cast<size_t>(y) * w * 3]),
                            static_cast<std::streamsize>(w) * 3);
                std::cout << "shot: wrote " << w << "x" << h << " PPM to " << shot_path << "\n";
            }
        }

        glfwSwapBuffers(window_);
        if (smoke && ++frame >= smoke_frames) break;
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window_);
    glfwTerminate();

    if (smoke) std::cout << "smoke: rendered " << frame << " frame(s) OK\n";
    return 0;
}

} // namespace z80::dbg
