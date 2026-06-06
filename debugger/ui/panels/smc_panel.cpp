//
// Z80 Digital Twin Debugger - Self-modifying-code panel implementation
// Copyright (c) 2025 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//

#include "smc_panel.h"
#include "ui_context.h"

#include "imgui.h"

#include <cstdio>
#include <string>

namespace z80::dbg {
namespace {

// "NAME (0xADDR)" if a symbol resolves, else "0xADDR".
std::string addr_label(UiContext& ctx, uint16_t a) {
    char buf[48];
    if (auto name = ctx.symbols.ResolveName(a))
        std::snprintf(buf, sizeof(buf), "%s (0x%04X)", name->c_str(), a);
    else
        std::snprintf(buf, sizeof(buf), "0x%04X", a);
    return buf;
}

} // namespace

void SmcPanel::Draw(UiContext& ctx) {
    ImGui::SetNextWindowPos(ImVec2(1290, 500), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(310, 488), ImGuiCond_FirstUseEver);
    ImGui::Begin("Self-Modifying Code");

    bool brk = ctx.session.BreakOnSmc();
    if (ImGui::Checkbox("Break on SMC", &brk)) ctx.session.SetBreakOnSmc(brk);
    ImGui::SameLine();
    ImGui::Text("events: %llu", static_cast<unsigned long long>(ctx.session.SmcCount()));

    const auto& events = ctx.session.SmcEvents();
    if (events.empty()) {
        ImGui::TextDisabled("No self-modifying writes detected yet.");
        ImGui::End();
        return;
    }

    if (ImGui::BeginTable("smc", 4,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY |
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("Writer");
        ImGui::TableSetupColumn("Target");
        ImGui::TableSetupColumn("Change");
        ImGui::TableSetupColumn("Cycle");
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        // Most recent first.
        for (std::size_t i = events.size(); i-- > 0;) {
            const SmcEvent& e = events[i];
            ImGui::TableNextRow();
            ImGui::PushID(static_cast<int>(i));

            ImGui::TableSetColumnIndex(0);
            // Selecting the writer jumps the disassembly to the modifying code.
            if (ImGui::Selectable(addr_label(ctx, e.writer_pc).c_str(), false,
                                  ImGuiSelectableFlags_SpanAllColumns)) {
                ctx.disasm_goto = e.writer_pc;
            }

            ImGui::TableSetColumnIndex(1);
            // A right-click jumps to the modified target instead.
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 1.0f, 1.0f), "%s",
                               addr_label(ctx, e.address).c_str());
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) ctx.disasm_goto = e.address;

            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%02X -> %02X", e.old_value, e.new_value);

            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%llu", static_cast<unsigned long long>(e.cycle));

            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    if (ctx.session.SmcCount() > events.size()) {
        ImGui::TextDisabled("(showing newest %llu of %llu)",
                            static_cast<unsigned long long>(events.size()),
                            static_cast<unsigned long long>(ctx.session.SmcCount()));
    }
    ImGui::End();
}

} // namespace z80::dbg
