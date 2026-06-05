//
// Z80 Digital Twin Debugger - Disassembly panel implementation
// Copyright (c) 2025 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//

#include "disassembly_panel.h"
#include "ui_context.h"

#include "imgui.h"

#include <format>
#include <string>

namespace z80::dbg {

void DisassemblyPanel::Draw(UiContext& ctx) {
    ImGui::SetNextWindowPos(ImVec2(0, 374), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(520, 614), ImGuiCond_FirstUseEver);
    ImGui::Begin("Disassembly");

    ImGui::Checkbox("Follow PC", &follow_pc_);
    ImGui::SameLine();
    ImGui::TextDisabled("(click the gutter to toggle a breakpoint)");

    DebugSession& session = ctx.session;
    if (follow_pc_ && session.State() != RunState::Running) {
        top_ = ctx.cpu().PC();
    }

    const ByteReader read = ctx.reader();
    const SymbolResolver resolve = ctx.resolver();
    const uint16_t pc = ctx.cpu().PC();

    if (ImGui::BeginTable("disasm", 4,
                          ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("BP",    ImGuiTableColumnFlags_WidthFixed, 16);
        ImGui::TableSetupColumn("Addr",  ImGuiTableColumnFlags_WidthFixed, 64);
        ImGui::TableSetupColumn("Bytes", ImGuiTableColumnFlags_WidthFixed, 110);
        ImGui::TableSetupColumn("Instruction", ImGuiTableColumnFlags_WidthStretch);

        uint16_t addr = top_;
        for (int line = 0; line < 256; ++line) {
            const Instruction ins = ctx.disasm.Decode(read, addr, resolve);
            ImGui::TableNextRow();
            if (addr == pc) {
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                                       ImGui::GetColorU32(ImVec4(0.20f, 0.35f, 0.55f, 0.65f)));
            }
            ImGui::PushID(addr);

            // BP gutter: a red "@" marks a breakpoint. Clicking adds one;
            // clicking it again removes it (a true add/remove toggle).
            ImGui::TableSetColumnIndex(0);
            const bool has_bp = session.HasBreakpoint(addr);
            if (has_bp)
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
            if (ImGui::Selectable(has_bp ? "@" : " ", false, 0, ImVec2(12, 0))) {
                if (has_bp) session.RemoveBreakpoint(addr);
                else        session.AddBreakpoint(addr);
            }
            if (has_bp) ImGui::PopStyleColor();

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
            if (auto label = ctx.symbols.ResolveName(addr)) {
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

} // namespace z80::dbg
