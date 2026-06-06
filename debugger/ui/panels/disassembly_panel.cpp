//
// Z80 Digital Twin Debugger - Disassembly panel implementation
// Copyright (c) 2025 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//

#include "disassembly_panel.h"
#include "ui_context.h"
#include "symbol_style.h"

#include "imgui.h"

#include <algorithm>
#include <format>
#include <optional>
#include <string>
#include <vector>

namespace z80::dbg {
namespace {

// Render an operand string, colouring substituted symbol names by type and
// showing a description tooltip when hovered.
void draw_operands(UiContext& ctx, const Instruction& ins) {
    const std::string& ops = ins.operands;

    struct Hit { size_t pos; size_t len; Symbol sym; };
    std::vector<Hit> hits;
    for (const auto& name : ins.symbols_used) {
        if (name.empty()) continue;
        auto addr = ctx.symbols.Resolve(name);
        if (!addr) continue;
        auto sym = ctx.symbols.Lookup(*addr);
        if (!sym) continue;
        for (size_t p = ops.find(name); p != std::string::npos; p = ops.find(name, p + name.size()))
            hits.push_back({p, name.size(), *sym});
    }
    std::sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.pos < b.pos; });

    bool first = true;
    auto plain = [&](const std::string& seg) {
        if (seg.empty()) return;
        if (!first) ImGui::SameLine(0, 0);
        first = false;
        ImGui::TextUnformatted(seg.c_str());
    };

    size_t cursor = 0;
    for (const auto& h : hits) {
        if (h.pos < cursor) continue;   // overlapping match: skip
        plain(ops.substr(cursor, h.pos - cursor));
        if (!first) ImGui::SameLine(0, 0);
        first = false;
        ImGui::TextColored(SymbolColor(h.sym.type), "%s", ops.substr(h.pos, h.len).c_str());
        SymbolTooltipIfHovered(h.sym);
        cursor = h.pos + h.len;
    }
    plain(ops.substr(cursor));
    if (first) ImGui::TextUnformatted("");   // ensure the row has content
}

} // namespace

void DisassemblyPanel::Draw(UiContext& ctx) {
    ImGui::SetNextWindowPos(ImVec2(0, 374), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(520, 614), ImGuiCond_FirstUseEver);
    ImGui::Begin("Disassembly");

    ImGui::Checkbox("Follow PC", &follow_pc_);
    ImGui::SameLine();
    ImGui::TextDisabled("(gutter = breakpoint; right-click an address to label it)");

    DebugSession& session = ctx.session;
    if (follow_pc_ && session.State() != RunState::Running) {
        top_ = ctx.cpu().PC();
    }

    const ByteReader read = ctx.reader();
    const SymbolResolver resolve = ctx.resolver();
    const uint16_t pc = ctx.cpu().PC();

    if (ImGui::BeginTable("disasm", 5,
                          ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("BP",    ImGuiTableColumnFlags_WidthFixed, 16);
        ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 104);
        ImGui::TableSetupColumn("Addr",  ImGuiTableColumnFlags_WidthFixed, 48);
        ImGui::TableSetupColumn("Bytes", ImGuiTableColumnFlags_WidthFixed, 104);
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

            // BP gutter: red "@" marks a breakpoint; click toggles add/remove.
            ImGui::TableSetColumnIndex(0);
            const bool has_bp = session.HasBreakpoint(addr);
            if (has_bp)
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
            if (ImGui::Selectable(has_bp ? "@" : " ", false, 0, ImVec2(12, 0))) {
                if (has_bp) session.RemoveBreakpoint(addr);
                else        session.AddBreakpoint(addr);
            }
            if (has_bp) ImGui::PopStyleColor();

            // Label column: code symbols, coloured, with a description tooltip.
            ImGui::TableSetColumnIndex(1);
            if (auto sym = ctx.symbols.Lookup(addr); sym && IsCodeLabel(sym->type)) {
                ImGui::TextColored(SymbolColor(sym->type), "%s", sym->name.c_str());
                SymbolTooltipIfHovered(*sym);
            }

            // Address (right-click to label this address)
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%04X", addr);
            if (ImGui::BeginPopupContextItem("lbl")) {
                if (ImGui::IsWindowAppearing()) PrimeSymbolEdit(edit_, addr, ctx.symbols);
                DrawSymbolEditForm(ctx, edit_);
                ImGui::EndPopup();
            }

            // Bytes
            ImGui::TableSetColumnIndex(3);
            std::string hexbytes;
            for (int b = 0; b < ins.length && b < 4; ++b)
                hexbytes += std::format("{:02X} ", ins.bytes[b]);
            ImGui::TextUnformatted(hexbytes.c_str());

            // Instruction: mnemonic + colour-coded operand symbols (with tooltips).
            ImGui::TableSetColumnIndex(4);
            if (addr == pc) {
                ImGui::TextColored(ImVec4(1, 1, 0.6f, 1), "%s", ins.text.c_str());
            } else {
                ImGui::TextUnformatted(ins.mnemonic.c_str());
                if (!ins.operands.empty()) {
                    ImGui::SameLine();
                    draw_operands(ctx, ins);
                }
            }

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
