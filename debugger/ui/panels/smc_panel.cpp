//
// Z80 Digital Twin Debugger - Self-modifying-code panel implementation
// Copyright (c) 2025 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//
// Two related-but-distinct categories:
//   * Self-modifying code — a committed write to a byte that executed as code
//     (RAM). Shown magenta.
//   * Blocked ROM writes — a write to write-protected memory that was *refused*
//     (the byte is unchanged). It resembles SMC but is semantically different
//     (read-only memory, not self-modifying code). Shown amber.
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

    const ImVec4 smc_col(1.0f, 0.4f, 1.0f, 1.0f);      // magenta — real SMC (RAM)
    const ImVec4 blocked_col(1.0f, 0.65f, 0.1f, 1.0f); // amber — refused ROM write

    // -- Self-modifying code (committed writes to executed RAM) --------------
    bool brk = ctx.session.BreakOnSmc();
    if (ImGui::Checkbox("Break on SMC", &brk)) ctx.session.SetBreakOnSmc(brk);
    ImGui::SameLine();
    ImGui::Text("SMC: %llu", static_cast<unsigned long long>(ctx.session.SmcCount()));

    const auto& events = ctx.session.SmcEvents();
    if (events.empty()) {
        ImGui::TextDisabled("No self-modifying writes detected yet.");
    } else if (ImGui::BeginTable("smc", 4,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY |
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit,
                          ImVec2(0, 180))) {
        ImGui::TableSetupColumn("Writer");
        ImGui::TableSetupColumn("Target");
        ImGui::TableSetupColumn("Change");
        ImGui::TableSetupColumn("Cycle");
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();
        for (std::size_t i = events.size(); i-- > 0;) {
            const SmcEvent& e = events[i];
            ImGui::TableNextRow();
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableSetColumnIndex(0);
            if (ImGui::Selectable(addr_label(ctx, e.writer_pc).c_str(), false,
                                  ImGuiSelectableFlags_SpanAllColumns))
                ctx.disasm_goto = e.writer_pc;
            ImGui::TableSetColumnIndex(1);
            ImGui::TextColored(smc_col, "%s", addr_label(ctx, e.address).c_str());
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) ctx.disasm_goto = e.address;
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%02X -> %02X", e.old_value, e.new_value);
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%llu", static_cast<unsigned long long>(e.cycle));
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    // -- Blocked ROM writes (refused writes to read-only memory) -------------
    ImGui::Separator();
    ImGui::TextColored(blocked_col, "Blocked ROM writes: %llu",
                       static_cast<unsigned long long>(ctx.session.BlockedWriteCount()));
    ImGui::TextDisabled("refused (read-only) — value unchanged; looks like SMC but isn't");

    const auto& blocked = ctx.session.BlockedWrites();
    if (blocked.empty()) {
        ImGui::TextDisabled("None.");
    } else if (ImGui::BeginTable("blocked", 4,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY |
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("Writer");
        ImGui::TableSetupColumn("Target");
        ImGui::TableSetupColumn("Tried");
        ImGui::TableSetupColumn("Cycle");
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();
        for (std::size_t i = blocked.size(); i-- > 0;) {
            const BlockedWrite& b = blocked[i];
            ImGui::TableNextRow();
            ImGui::PushID(static_cast<int>(0x10000 + i));
            ImGui::TableSetColumnIndex(0);
            if (ImGui::Selectable(addr_label(ctx, b.writer_pc).c_str(), false,
                                  ImGuiSelectableFlags_SpanAllColumns))
                ctx.disasm_goto = b.writer_pc;
            ImGui::TableSetColumnIndex(1);
            ImGui::TextColored(blocked_col, "%s", addr_label(ctx, b.address).c_str());
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) ctx.disasm_goto = b.address;
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("wr %02X (kept %02X)", b.attempted_value, b.current_value);
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%llu", static_cast<unsigned long long>(b.cycle));
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    ImGui::End();
}

} // namespace z80::dbg
