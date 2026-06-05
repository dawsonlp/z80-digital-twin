//
// Z80 Digital Twin Debugger - Control panel implementation
// Copyright (c) 2025 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//

#include "control_panel.h"
#include "ui_context.h"

#include "imgui.h"

namespace z80::dbg {
namespace {

const char* state_text(RunState s) {
    switch (s) {
        case RunState::Paused:  return "PAUSED";
        case RunState::Running: return "RUNNING";
        case RunState::Halted:  return "HALTED";
    }
    return "?";
}

} // namespace

void ControlPanel::Draw(UiContext& ctx) {
    ImGui::SetNextWindowPos(ImVec2(0, 24), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(1600, 92), ImGuiCond_FirstUseEver);
    ImGui::Begin("Control");

    const bool halted = ctx.cpu().IsHalted();
    ImGui::BeginDisabled(halted);
    if (ImGui::Button("Step"))      ctx.commands.step = true;       ImGui::SameLine();
    if (ImGui::Button("Step Over")) ctx.commands.step_over = true;  ImGui::SameLine();
    if (ImGui::Button("Run"))       ctx.commands.run = true;        ImGui::SameLine();
    ImGui::EndDisabled();
    if (ImGui::Button("Pause"))     ctx.commands.pause = true;      ImGui::SameLine();
    if (ImGui::Button("Reset"))     ctx.commands.reset = true;

    ImGui::Separator();
    ImGui::Text("State: %s    PC: 0x%04X    Cycles: %llu    Halted: %s",
                state_text(ctx.session.State()),
                ctx.cpu().PC(),
                static_cast<unsigned long long>(ctx.cpu().GetCycleCount()),
                halted ? "yes" : "no");
    ImGui::TextUnformatted(ctx.status.c_str());
    ImGui::End();
}

} // namespace z80::dbg
