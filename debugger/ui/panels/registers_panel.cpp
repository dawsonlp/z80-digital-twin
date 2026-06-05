//
// Z80 Digital Twin Debugger - Registers panel implementation
// Copyright (c) 2025 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//

#include "registers_panel.h"
#include "ui_context.h"

#include "imgui.h"

namespace z80::dbg {

void RegistersPanel::Draw(UiContext& ctx) {
    ImGui::SetNextWindowPos(ImVec2(0, 120), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(520, 250), ImGuiCond_FirstUseEver);
    ImGui::Begin("Registers");

    DebugCPU& cpu = ctx.cpu();

    // Registers are editable when paused/halted; read-only while free-running
    // (so edits don't fight the executing program frame-to-frame).
    const bool running = ctx.session.State() == RunState::Running;
    if (running) ImGui::TextDisabled("(pause to edit registers)");

    if (ImGui::BeginTable("regs", 4, ImGuiTableFlags_SizingFixedFit)) {
        auto cell = [&](const char* label, uint16_t* reg) {
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(label);
            ImGui::SameLine();
            ImGui::PushID(reg);
            if (running) {
                ImGui::Text("%04X", *reg);
            } else {
                ImGui::SetNextItemWidth(48);
                ImGui::InputScalar("##r", ImGuiDataType_U16, reg, nullptr, nullptr,
                                   "%04X", ImGuiInputTextFlags_CharsHexadecimal);
            }
            ImGui::PopID();
        };
        cell("AF ", &cpu.AF());  cell("AF'", &cpu.AltAF());
        cell("BC ", &cpu.BC());  cell("BC'", &cpu.AltBC());
        cell("DE ", &cpu.DE());  cell("DE'", &cpu.AltDE());
        cell("HL ", &cpu.HL());  cell("HL'", &cpu.AltHL());
        cell("IX ", &cpu.IX());  cell("IY ", &cpu.IY());
        cell("PC ", &cpu.PC());  cell("SP ", &cpu.SP());
        cell("IR ", &cpu.IR());  cell("WZ ", &cpu.WZ());
        ImGui::EndTable();
    }

    ImGui::Separator();
    const uint8_t f = cpu.F();
    auto flag = [&](const char* name, uint8_t mask) {
        const bool set = (f & mask) != 0;
        ImGui::TextColored(set ? ImVec4(0.4f, 1.0f, 0.4f, 1.0f)
                               : ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                           "%s", name);
        ImGui::SameLine();
    };
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
                cpu.InterruptMode(), cpu.IFF1() ? 1 : 0, cpu.IFF2() ? 1 : 0);
    ImGui::End();
}

} // namespace z80::dbg
