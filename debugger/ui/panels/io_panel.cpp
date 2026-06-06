//
// Z80 Digital Twin Debugger - I/O bus panel implementation
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
    ImGui::Begin("I/O Bus");

    // I/O is a sequence of bus transactions, not a table of stored values:
    // OUT is a transient write, IN reads a device's live state, and reading a
    // port can have side effects — so we show what the *program* did and never
    // poll ports ourselves. The transaction log comes from ObservableIo.
    auto& io = ctx.cpu().GetIo();
    const auto& transactions = io.Transactions();

    ImGui::Text("transactions: %llu", static_cast<unsigned long long>(io.TransactionCount()));
    ImGui::SameLine();
    if (ImGui::Button("Clear")) io.ClearTransactions();
    ImGui::SameLine();
    ImGui::TextDisabled("(bus activity; not stored port values)");

    if (transactions.empty()) {
        ImGui::TextDisabled("No I/O performed yet.");
        ImGui::End();
        return;
    }

    if (ImGui::BeginTable("io", 4,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY |
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("Seq");
        ImGui::TableSetupColumn("Dir");
        ImGui::TableSetupColumn("Port");
        ImGui::TableSetupColumn("Value");
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        // Most recent first.
        for (std::size_t i = transactions.size(); i-- > 0;) {
            const auto& t = transactions[i];
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%llu", static_cast<unsigned long long>(t.seq));
            ImGui::TableSetColumnIndex(1);
            if (t.is_out) ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.4f, 1.0f), "OUT");
            else          ImGui::TextColored(ImVec4(0.5f, 0.85f, 1.0f, 1.0f), "IN");
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("0x%04X", t.port);
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("0x%02X (%u)", t.value, t.value);
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

} // namespace z80::dbg
