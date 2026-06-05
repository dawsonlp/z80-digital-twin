//
// Z80 Digital Twin Debugger - DebuggerApp implementation
// Copyright (c) 2025 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//

#include "debugger_app.h"

#define GL_SILENCE_DEPRECATION
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
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

const char* state_text(RunState s) {
    switch (s) {
        case RunState::Paused:  return "PAUSED";
        case RunState::Running: return "RUNNING";
        case RunState::Halted:  return "HALTED";
    }
    return "?";
}

bool parse_hex16(const char* s, uint16_t& out) {
    try {
        const unsigned long v = std::stoul(s, nullptr, 16);
        out = static_cast<uint16_t>(v & 0xFFFF);
        return true;
    } catch (...) {
        return false;
    }
}

void glfw_error_callback(int error, const char* description) {
    std::cerr << "GLFW error " << error << ": " << description << "\n";
}

} // namespace

DebuggerApp::DebuggerApp() = default;

ByteReader DebuggerApp::Reader() const {
    return [this](uint16_t a) { return cpu_.ReadMemory(a); };
}

SymbolResolver DebuggerApp::Resolver() const {
    return symbols_.MakeResolver();
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
    last_status_ = std::format("Loaded {} bytes from {}", bytes.size(), path);
    return true;
}

bool DebuggerApp::LoadSymbolFile(const std::string& path) {
    std::vector<std::string> warnings;
    const bool ok = symbols_.LoadFromFile(path, nullptr, &warnings);
    for (const auto& w : warnings) std::cerr << "symbols: " << w << "\n";
    if (ok) last_status_ = std::format("Loaded {} symbols from {}", symbols_.Size(), path);
    return ok;
}

void DebuggerApp::LoadDemo() {
    // GCD by repeated subtraction (result in HL); inputs preset below.
    const std::vector<uint8_t> program = {
        0x7A,             // LD A, D
        0xB3,             // OR E
        0x28, 0x0B,       // JR Z, 0x000F
        0xB7,             // OR A
        0xED, 0x52,       // SBC HL, DE
        0x30, 0x02,       // JR NC, 0x000B
        0x19,             // ADD HL, DE
        0xEB,             // EX DE, HL
        0x18, 0xF3,       // JR 0x0000
        0x18, 0xF1,       // JR 0x0000
        0x76,             // HALT
    };
    cpu_.Reset();
    cpu_.LoadProgram(program, 0x0000);
    cpu_.HL() = 1071;     // GCD(1071, 462) = 21
    cpu_.DE() = 462;
    session_.ClearDirty();   // program load isn't a "change" to highlight

    symbols_.DefineLabel(0x0000, "GCD_LOOP", SymbolType::Function, "GCD by subtraction");
    symbols_.DefineLabel(0x000F, "DONE", SymbolType::Label, "result in HL");
    last_status_ = "Loaded built-in GCD demo (HL=1071, DE=462)";
}

// ===========================================================================
// Command handling
// ===========================================================================

void DebuggerApp::BeforeExecAction() {
    session_.ClearDirty();
}

void DebuggerApp::ExecuteCommands() {
    if (cmd_reset_) {
        session_.Reset();
        last_status_ = "Reset";
    }
    if (cmd_step_) {
        BeforeExecAction();
        const StepResult r = session_.StepInstruction();
        last_status_ = std::format("Step: {} @ 0x{:04X} (+{} T)",
                                   reason_text(r.reason), r.pc, r.cycles);
    }
    if (cmd_step_over_) {
        BeforeExecAction();
        const StepResult r = session_.StepOver();
        last_status_ = std::format("Step over: {} @ 0x{:04X} (+{} T)",
                                   reason_text(r.reason), r.pc, r.cycles);
    }
    if (cmd_run_) {
        BeforeExecAction();
        session_.Run();
        last_status_ = "Running...";
    }
    if (cmd_pause_) {
        session_.Pause();
        last_status_ = "Paused";
    }

    cmd_reset_ = cmd_step_ = cmd_step_over_ = cmd_run_ = cmd_pause_ = false;

    // While running, advance a bounded slice per frame.
    if (session_.State() == RunState::Running) {
        const StepResult r = session_.RunSlice(run_budget_);
        if (session_.State() != RunState::Running) {
            last_status_ = std::format("Stopped: {} @ 0x{:04X}",
                                       reason_text(r.reason), r.pc);
        }
    }
}

// ===========================================================================
// Panels
// ===========================================================================

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

void DebuggerApp::DrawControlBar() {
    ImGui::SetNextWindowPos(ImVec2(0, 24), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(1600, 92), ImGuiCond_FirstUseEver);
    ImGui::Begin("Control");

    const bool halted = cpu_.IsHalted();
    ImGui::BeginDisabled(halted);
    if (ImGui::Button("Step"))      cmd_step_ = true;       ImGui::SameLine();
    if (ImGui::Button("Step Over")) cmd_step_over_ = true;  ImGui::SameLine();
    if (ImGui::Button("Run"))       cmd_run_ = true;        ImGui::SameLine();
    ImGui::EndDisabled();
    if (ImGui::Button("Pause"))     cmd_pause_ = true;      ImGui::SameLine();
    if (ImGui::Button("Reset"))     cmd_reset_ = true;

    ImGui::Separator();
    ImGui::Text("State: %s    PC: 0x%04X    Cycles: %llu    Halted: %s",
                state_text(session_.State()),
                cpu_.PC(),
                static_cast<unsigned long long>(cpu_.GetCycleCount()),
                halted ? "yes" : "no");
    ImGui::TextUnformatted(last_status_.c_str());
    ImGui::End();
}

void DebuggerApp::DrawRegisters() {
    ImGui::SetNextWindowPos(ImVec2(0, 120), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(520, 250), ImGuiCond_FirstUseEver);
    ImGui::Begin("Registers");

    const uint8_t f = cpu_.F();
    auto flag = [&](const char* name, uint8_t mask) {
        const bool set = (f & mask) != 0;
        ImGui::TextColored(set ? ImVec4(0.4f, 1.0f, 0.4f, 1.0f)
                               : ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                           "%s", name);
        ImGui::SameLine();
    };

    if (ImGui::BeginTable("regs", 4,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit)) {
        auto cell = [](const char* label, unsigned value) {
            ImGui::TableNextColumn();
            ImGui::Text("%-3s %04X", label, value & 0xFFFF);
        };
        cell("AF", cpu_.AF());  cell("AF'", cpu_.AltAF());
        cell("BC", cpu_.BC());  cell("BC'", cpu_.AltBC());
        cell("DE", cpu_.DE());  cell("DE'", cpu_.AltDE());
        cell("HL", cpu_.HL());  cell("HL'", cpu_.AltHL());
        cell("IX", cpu_.IX());  cell("IY",  cpu_.IY());
        cell("PC", cpu_.PC());  cell("SP",  cpu_.SP());
        cell("IR", cpu_.IR());  cell("WZ",  cpu_.WZ());
        ImGui::EndTable();
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Flags: ");
    ImGui::SameLine();
    flag("S", Constants::Flags::SIGN);
    flag("Z", Constants::Flags::ZERO);
    flag("H", Constants::Flags::HALF);
    flag("P/V", Constants::Flags::PARITY);
    flag("N", Constants::Flags::SUBTRACT);
    flag("C", Constants::Flags::CARRY);
    ImGui::NewLine();

    ImGui::Text("IM %u    IFF1 %d  IFF2 %d",
                cpu_.InterruptMode(), cpu_.IFF1() ? 1 : 0, cpu_.IFF2() ? 1 : 0);
    ImGui::End();
}

void DebuggerApp::DrawDisassembly() {
    ImGui::SetNextWindowPos(ImVec2(0, 374), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(520, 614), ImGuiCond_FirstUseEver);
    ImGui::Begin("Disassembly");

    ImGui::Checkbox("Follow PC", &follow_pc_);
    ImGui::SameLine();
    ImGui::TextDisabled("(click the gutter to toggle a breakpoint)");

    if (follow_pc_ && session_.State() != RunState::Running) {
        disasm_top_ = cpu_.PC();
    }

    const ByteReader read = Reader();
    const SymbolResolver resolve = Resolver();
    const uint16_t pc = cpu_.PC();

    if (ImGui::BeginTable("disasm", 4,
                          ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("BP",    ImGuiTableColumnFlags_WidthFixed, 16);
        ImGui::TableSetupColumn("Addr",  ImGuiTableColumnFlags_WidthFixed, 64);
        ImGui::TableSetupColumn("Bytes", ImGuiTableColumnFlags_WidthFixed, 110);
        ImGui::TableSetupColumn("Instruction", ImGuiTableColumnFlags_WidthStretch);

        uint16_t addr = disasm_top_;
        for (int line = 0; line < 256; ++line) {
            const Instruction ins = disasm_.Decode(read, addr, resolve);
            ImGui::TableNextRow();
            if (addr == pc) {
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                                       ImGui::GetColorU32(ImVec4(0.20f, 0.35f, 0.55f, 0.65f)));
            }
            ImGui::PushID(addr);

            // BP gutter
            ImGui::TableSetColumnIndex(0);
            const bool has_bp = session_.HasBreakpoint(addr);
            if (ImGui::Selectable(has_bp ? "●" : " ", false,
                                  ImGuiSelectableFlags_None)) {
                session_.ToggleBreakpoint(addr);
            }

            // Address
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%04X", addr);

            // Bytes
            ImGui::TableSetColumnIndex(2);
            std::string hexbytes;
            for (int b = 0; b < ins.length && b < 4; ++b)
                hexbytes += std::format("{:02X} ", ins.bytes[b]);
            ImGui::TextUnformatted(hexbytes.c_str());

            // Instruction column: optional "LABEL:" line above the mnemonic.
            ImGui::TableSetColumnIndex(3);
            if (auto label = symbols_.ResolveName(addr)) {
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 1.0f, 1.0f), "%s:", label->c_str());
            }
            if (addr == pc) ImGui::TextColored(ImVec4(1, 1, 0.6f, 1), "%s", ins.text.c_str());
            else            ImGui::TextUnformatted(ins.text.c_str());

            ImGui::PopID();

            const uint16_t next = static_cast<uint16_t>(addr + (ins.length ? ins.length : 1));
            if (next < addr) break;   // 64K wrap: stop
            addr = next;
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

void DebuggerApp::DrawMemory() {
    ImGui::SetNextWindowPos(ImVec2(525, 120), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(760, 868), ImGuiCond_FirstUseEver);
    ImGui::Begin("Memory");

    ImGui::SetNextItemWidth(80);
    if (ImGui::InputTextWithHint("##memgoto", "addr", mem_goto_buf_, sizeof(mem_goto_buf_),
                                 ImGuiInputTextFlags_CharsHexadecimal |
                                 ImGuiInputTextFlags_EnterReturnsTrue)) {
        if (parse_hex16(mem_goto_buf_, mem_goto_addr_)) mem_goto_pending_ = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Go")) {
        if (parse_hex16(mem_goto_buf_, mem_goto_addr_)) mem_goto_pending_ = true;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("changed bytes are highlighted");

    const auto& dirty = session_.DirtyAddresses();
    const ImVec4 dirty_col(1.0f, 0.5f, 0.4f, 1.0f);

    if (ImGui::BeginChild("memscroll", ImVec2(0, 0), false,
                          ImGuiWindowFlags_HorizontalScrollbar)) {
        const float line_h = ImGui::GetTextLineHeightWithSpacing();
        if (mem_goto_pending_) {
            ImGui::SetScrollY((mem_goto_addr_ / 16) * line_h);
            mem_goto_pending_ = false;
        }

        ImGuiListClipper clipper;
        clipper.Begin(4096, line_h);   // 65536 / 16 rows
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                const uint16_t base = static_cast<uint16_t>(row * 16);
                ImGui::Text("%04X ", base);
                ImGui::SameLine();
                // hex
                for (int c = 0; c < 16; ++c) {
                    const uint16_t a = static_cast<uint16_t>(base + c);
                    const uint8_t v = cpu_.ReadMemory(a);
                    if (dirty.count(a))
                        ImGui::TextColored(dirty_col, "%02X", v);
                    else
                        ImGui::Text("%02X", v);
                    ImGui::SameLine();
                }
                // ascii
                ImGui::Text(" ");
                ImGui::SameLine();
                std::string ascii;
                for (int c = 0; c < 16; ++c) {
                    const uint8_t v = cpu_.ReadMemory(static_cast<uint16_t>(base + c));
                    ascii.push_back((v >= 32 && v < 127) ? static_cast<char>(v) : '.');
                }
                ImGui::TextUnformatted(ascii.c_str());
            }
        }
        clipper.End();
    }
    ImGui::EndChild();
    ImGui::End();
}

void DebuggerApp::DrawIoPorts() {
    ImGui::SetNextWindowPos(ImVec2(1290, 120), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(310, 868), ImGuiCond_FirstUseEver);
    ImGui::Begin("I/O Ports");
    if (ImGui::BeginTable("io", 3,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY |
                          ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("Port");
        ImGui::TableSetupColumn("Hex");
        ImGui::TableSetupColumn("Dec");
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        ImGuiListClipper clipper;
        clipper.Begin(256);
        while (clipper.Step()) {
            for (int p = clipper.DisplayStart; p < clipper.DisplayEnd; ++p) {
                const uint8_t v = cpu_.ReadPort(static_cast<uint8_t>(p));
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::Text("%02X", p);
                ImGui::TableSetColumnIndex(1); ImGui::Text("%02X", v);
                ImGui::TableSetColumnIndex(2); ImGui::Text("%u", v);
            }
        }
        clipper.End();
        ImGui::EndTable();
    }
    ImGui::End();
}

void DebuggerApp::DrawUi() {
    DrawMenuBar();
    DrawControlBar();
    DrawRegisters();
    DrawDisassembly();
    DrawMemory();
    DrawIoPorts();
}

// ===========================================================================
// Run loop
// ===========================================================================

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
    // In smoke/shot mode use the built-in default layout (no persisted .ini),
    // so automated screenshots are deterministic.
    if (smoke) ImGui::GetIO().IniFilename = nullptr;
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

        DrawUi();

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
