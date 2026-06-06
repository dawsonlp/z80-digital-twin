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
#include <cstdio>
#include <cstdlib>
#include <format>
#include <optional>
#include <string>
#include <vector>

namespace z80::dbg {
namespace {

bool parse_hex16(const char* s, uint16_t& out) {
    try {
        out = static_cast<uint16_t>(std::stoul(s, nullptr, 16) & 0xFFFF);
        return true;
    } catch (...) {
        return false;
    }
}

// Resolve a go-to entry: a symbol name (preferred, so "FLAGS" beats hex "FA"),
// otherwise a hex address ("5C3B" or "0x5C3B").
bool resolve_goto(UiContext& ctx, const char* text, uint16_t& out) {
    if (!text || text[0] == '\0') return false;
    if (auto addr = ctx.symbols.Resolve(text)) { out = *addr; return true; }
    return parse_hex16(text, out);
}

// Render an operand string, colouring substituted symbol names by type, showing
// a description tooltip on hover, and a "Go to" right-click menu. Returns an
// address to jump the view to if the user picked one this frame.
std::optional<uint16_t> draw_operands(UiContext& ctx, const Instruction& ins) {
    const std::string& ops = ins.operands;
    std::optional<uint16_t> goto_addr;

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
    int hit_id = 0;
    for (const auto& h : hits) {
        if (h.pos < cursor) continue;   // overlapping match: skip
        plain(ops.substr(cursor, h.pos - cursor));
        if (!first) ImGui::SameLine(0, 0);
        first = false;
        ImGui::TextColored(SymbolColor(h.sym.type), "%s", ops.substr(h.pos, h.len).c_str());
        SymbolTooltipIfHovered(h.sym);
        ImGui::PushID(hit_id++);
        if (ImGui::BeginPopupContextItem("opmenu")) {
            char label[64];
            std::snprintf(label, sizeof(label), "Go to %s (0x%04X)",
                          h.sym.name.c_str(), h.sym.address);
            if (ImGui::MenuItem(label)) goto_addr = h.sym.address;
            ImGui::EndPopup();
        }
        ImGui::PopID();
        cursor = h.pos + h.len;
    }
    plain(ops.substr(cursor));
    if (first) ImGui::TextUnformatted("");   // ensure the row has content
    return goto_addr;
}

} // namespace

void DisassemblyPanel::Draw(UiContext& ctx) {
    ImGui::SetNextWindowPos(ImVec2(0, 374), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(520, 614), ImGuiCond_FirstUseEver);
    ImGui::Begin("Disassembly");

    // -- Toolbar: history, go-to (hex or symbol), Follow PC ------------------
    auto navigate = [&](uint16_t addr) {
        if (addr == top_ && !follow_pc_) return;   // already there
        back_.push_back(top_);
        forward_.clear();
        top_ = addr;
        follow_pc_ = false;
    };
    auto go_back = [&]() {
        if (back_.empty()) return;
        forward_.push_back(top_);
        top_ = back_.back();
        back_.pop_back();
        follow_pc_ = false;
    };
    auto go_forward = [&]() {
        if (forward_.empty()) return;
        back_.push_back(top_);
        top_ = forward_.back();
        forward_.pop_back();
        follow_pc_ = false;
    };

    ImGui::BeginDisabled(back_.empty());
    if (ImGui::Button("<")) go_back();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(forward_.empty());
    if (ImGui::Button(">")) go_forward();
    ImGui::EndDisabled();
    ImGui::SameLine();

    ImGui::TextUnformatted("Go to");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(130);
    uint16_t target = 0;
    auto submit = [&]() {
        if (resolve_goto(ctx, goto_buf_, target)) navigate(target);
        else if (goto_buf_[0]) ctx.status = std::string("Unknown address/symbol: ") + goto_buf_;
    };
    if (ImGui::InputTextWithHint("##disgoto", "hex or symbol", goto_buf_, sizeof(goto_buf_),
                                 ImGuiInputTextFlags_EnterReturnsTrue)) submit();
    ImGui::SameLine();
    if (ImGui::Button("Go")) submit();
    ImGui::SameLine();
    ImGui::Checkbox("Follow PC", &follow_pc_);

    DebugSession& session = ctx.session;
    if (follow_pc_ && session.State() != RunState::Running) {
        top_ = ctx.cpu().PC();
    }

    const ByteReader read = ctx.reader();
    const SymbolResolver resolve = ctx.resolver();
    const uint16_t pc = ctx.cpu().PC();

    std::optional<uint16_t> jump_request;   // applied after the table is built

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

            // Address: right-click for line actions (go to target, BP, label).
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%04X", addr);
            if (ImGui::BeginPopupContextItem("rowmenu")) {
                if (ins.branch_target) {
                    const uint16_t t = *ins.branch_target;
                    char label[64];
                    if (auto name = ctx.symbols.ResolveName(t))
                        std::snprintf(label, sizeof(label), "Go to target  0x%04X (%s)", t, name->c_str());
                    else
                        std::snprintf(label, sizeof(label), "Go to target  0x%04X", t);
                    if (ImGui::MenuItem(label)) jump_request = t;
                }
                if (ImGui::MenuItem(has_bp ? "Remove breakpoint" : "Add breakpoint")) {
                    if (has_bp) session.RemoveBreakpoint(addr);
                    else        session.AddBreakpoint(addr);
                }
                ImGui::Separator();
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

            // Instruction: mnemonic + colour-coded operand symbols (with tooltips
            // and a "Go to" right-click on the target name).
            ImGui::TableSetColumnIndex(4);
            ImGui::TextUnformatted(ins.mnemonic.c_str());
            if (!ins.operands.empty()) {
                ImGui::SameLine();
                if (auto g = draw_operands(ctx, ins)) jump_request = g;
            }

            ImGui::PopID();

            const uint16_t next = static_cast<uint16_t>(addr + (ins.length ? ins.length : 1));
            if (next < addr) break;   // 64K wrap: stop
            addr = next;
        }
        ImGui::EndTable();
    }

    if (jump_request) navigate(*jump_request);
    ImGui::End();
}

} // namespace z80::dbg
