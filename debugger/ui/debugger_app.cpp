//
// Z80 Digital Twin Debugger - DebuggerApp implementation
// Copyright (c) 2025 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//

#include "debugger_app.h"

#include "panels/control_panel.h"
#include "panels/registers_panel.h"
#include "panels/disassembly_panel.h"
#include "panels/memory_panel.h"
#include "panels/io_panel.h"

#define GL_SILENCE_DEPRECATION
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>

#include <cstdint>
#include <fstream>
#include <format>
#include <iostream>
#include <string>
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
        case StopReason::AlreadyHalted:   return "already halted";
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
}

UiContext DebuggerApp::MakeContext() {
    return UiContext{session_, symbols_, disasm_, commands_, status_};
}

bool DebuggerApp::LoadProgramFile(const std::string& path, uint16_t start_address) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::cerr << "Could not open program: " << path << "\n";
        return false;
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
    cpu_.Reset();
    cpu_.LoadProgram(bytes, start_address);
    session_.ClearDirty();   // program load isn't a "change" to highlight
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
    cpu_.Reset();
    cpu_.LoadProgram(program, 0x0000);
    cpu_.HL() = 1071;     // GCD(1071, 462) = 21
    cpu_.DE() = 462;
    session_.ClearDirty();   // program load isn't a "change" to highlight

    symbols_.DefineLabel(0x0000, "GCD_LOOP", SymbolType::Function,   "GCD by subtraction");
    symbols_.DefineLabel(0x000B, "NO_SWAP",  SymbolType::JumpTarget, "HL >= DE: loop without swap");
    symbols_.DefineLabel(0x000F, "DONE",     SymbolType::Label,      "store result and halt");
    symbols_.DefineLabel(0x9000, "RESULT",   SymbolType::WordVariable, "GCD result (16-bit)");
    status_ = "Loaded built-in GCD demo (HL=1071, DE=462)";
}

void DebuggerApp::AddBreakpoint(uint16_t address) {
    session_.AddBreakpoint(address);
}

void DebuggerApp::ExecuteCommands() {
    if (commands_.reset) {
        session_.Reset();
        status_ = "Reset";
    }
    if (commands_.step) {
        session_.ClearDirty();
        const StepResult r = session_.StepInstruction();
        status_ = std::format("Step: {} @ 0x{:04X} (+{} T)",
                              reason_text(r.reason), r.pc, r.cycles);
    }
    if (commands_.step_over) {
        session_.ClearDirty();
        const StepResult r = session_.StepOver();
        status_ = std::format("Step over: {} @ 0x{:04X} (+{} T)",
                              reason_text(r.reason), r.pc, r.cycles);
    }
    if (commands_.run) {
        session_.ClearDirty();
        session_.Run();
        status_ = "Running...";
    }
    if (commands_.pause) {
        session_.Pause();
        status_ = "Paused";
    }
    commands_.Clear();

    // While running, advance a bounded slice per frame.
    if (session_.State() == RunState::Running) {
        const StepResult r = session_.RunSlice(run_budget_);
        if (session_.State() != RunState::Running) {
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
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    if (smoke) ImGui::GetIO().IniFilename = nullptr;  // deterministic screenshots
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window_, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    int frame = 0;
    while (!glfwWindowShouldClose(window_)) {
        glfwPollEvents();
        ExecuteCommands();

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
