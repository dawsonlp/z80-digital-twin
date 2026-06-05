//
// Z80 Digital Twin Debugger - I/O ports panel implementation
// Copyright (c) 2025 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//

#include "io_panel.h"
#include "ui_context.h"

#include "imgui.h"

namespace z80::dbg {

void IoPanel::Draw(UiContext& ctx) {
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
                const uint8_t v = ctx.cpu().ReadPort(static_cast<uint8_t>(p));
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

} // namespace z80::dbg
