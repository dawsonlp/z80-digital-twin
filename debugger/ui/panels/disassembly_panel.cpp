//
// Z80 Digital Twin Debugger - Disassembly panel implementation
// Copyright (c) 2025-2026 Larry Dawson
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

namespace {
const char* evidence_text(EvidenceState state) {
    switch (state) {
        case EvidenceState::Observed: return "Observed";
        case EvidenceState::Modified: return "Modified";
        case EvidenceState::Unobserved: return "Unobserved";
        case EvidenceState::NotRetained: return "Not retained";
        case EvidenceState::PartialCapture: return "Partial capture";
    }
    return "?";
}
ImVec4 evidence_color(EvidenceState state) {
    if (state == EvidenceState::Observed) return {0.45f, 0.85f, 0.5f, 1};
    if (state == EvidenceState::Modified) return {1, 0.65f, 0.25f, 1};
    return {0.65f, 0.65f, 0.65f, 1};
}
bool manual_scroll() {
    if (!ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)) return false;
    const auto& io = ImGui::GetIO();
    const bool scrollbar = ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        io.MousePos.x >= ImGui::GetWindowPos().x + ImGui::GetWindowWidth() - ImGui::GetStyle().ScrollbarSize;
    return io.MouseWheel != 0 || scrollbar ||
        (ImGui::IsWindowFocused() && (ImGui::IsKeyPressed(ImGuiKey_PageUp) ||
          ImGui::IsKeyPressed(ImGuiKey_PageDown) || ImGui::IsKeyPressed(ImGuiKey_Home) ||
          ImGui::IsKeyPressed(ImGuiKey_End) || ImGui::IsKeyPressed(ImGuiKey_UpArrow) ||
          ImGui::IsKeyPressed(ImGuiKey_DownArrow)));
}
std::string byte_preview(const std::vector<uint8_t>& bytes) {
    std::string result;
    for (std::size_t i = 0; i < std::min(bytes.size(), std::size_t{4}); ++i)
        result += std::format("{:02X} ", bytes[i]);
    if (bytes.size() > 4) result += "...";
    return result;
}
}

void DisassemblyPanel::Draw(UiContext& ctx) {
    ImGui::SetNextWindowPos(ImVec2(0, 374), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(650, 614), ImGuiCond_FirstUseEver);
    ImGui::Begin("Disassembly");
    auto& session = ctx.session;
    const auto& history = session.History();
    const auto& memory = ctx.cpu().GetMemory();
    const uint16_t pc = ctx.cpu().PC();
    auto navigate = [&](uint16_t address) {
        back_.push_back(top_); forward_.clear();
        destination_ = address; scroll_target_ = address;
        follow_pc_ = false; history_view_ = false;
    };
    ImGui::BeginDisabled(back_.empty());
    if (ImGui::Button("<")) {
        forward_.push_back(top_); destination_ = back_.back(); back_.pop_back();
        scroll_target_ = destination_; follow_pc_ = false; history_view_ = false;
    }
    ImGui::EndDisabled(); ImGui::SameLine();
    ImGui::BeginDisabled(forward_.empty());
    if (ImGui::Button(">")) {
        back_.push_back(top_); destination_ = forward_.back(); forward_.pop_back();
        scroll_target_ = destination_; follow_pc_ = false; history_view_ = false;
    }
    ImGui::EndDisabled(); ImGui::SameLine();
    ImGui::SetNextItemWidth(110);
    bool go = ImGui::InputTextWithHint("##disgoto", "hex or symbol", goto_buf_, sizeof(goto_buf_),
                                      ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine(); go |= ImGui::Button("Go");
    if (go) {
        uint16_t address;
        if (resolve_goto(ctx, goto_buf_, address)) navigate(address);
        else if (goto_buf_[0]) ctx.status = std::string("Unknown address/symbol: ") + goto_buf_;
    }
    ImGui::SameLine();
    if (ImGui::Checkbox("Follow PC", &follow_pc_) && follow_pc_) {
        history_view_ = false; scroll_target_ = pc;
    }
    if (ImGui::RadioButton("Memory addresses", !history_view_)) history_view_ = false;
    ImGui::SameLine();
    if (ImGui::RadioButton("Execution history", history_view_)) history_view_ = true;
    if (ctx.disasm_goto) { navigate(*ctx.disasm_goto); ctx.disasm_goto.reset(); }

    if (history_view_) {
        ImGui::Checkbox("Follow latest", &follow_history_);
        ImGui::SameLine();
        ImGui::TextDisabled("%zu retained | %llu older dropped", history.Events().size(),
                           static_cast<unsigned long long>(history.Dropped()));
        ImGui::TextDisabled("Historical bytes; registers and memory remain live. Click address to inspect current memory.");
        if (ImGui::BeginTable("history", 6, ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg |
                             ImGuiTableFlags_SizingFixedFit)) {
            if (manual_scroll()) follow_history_ = false;
            ImGui::TableSetupColumn("Sequence"); ImGui::TableSetupColumn("Address");
            ImGui::TableSetupColumn("Bytes"); ImGui::TableSetupColumn("Instruction");
            ImGui::TableSetupColumn("Next PC / T"); ImGui::TableSetupColumn("Current bytes");
            ImGui::TableHeadersRow();
            const auto& events = history.Events();
            const float row_height = ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().CellPadding.y * 2;
            if (follow_history_) ImGui::SetScrollY(float(events.size() + 1) * row_height);
            ImGuiListClipper clipper; clipper.Begin(static_cast<int>(events.size()), row_height);
            while (clipper.Step()) for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                const auto& event = events[std::size_t(i)];
                ImGui::PushID(static_cast<int>(event.sequence));
                ImGui::TableNextRow(0, row_height); ImGui::TableSetColumnIndex(0);
                ImGui::Text("%llu", static_cast<unsigned long long>(event.sequence));
                ImGui::TableSetColumnIndex(1);
                const std::string address = std::format("{:04X}", event.start);
                if (ImGui::Selectable(address.c_str())) navigate(event.start);
                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(byte_preview(event.bytes).c_str());
                if (ImGui::IsItemHovered() && !event.bytes.empty()) {
                    ImGui::BeginTooltip();
                    ImGui::Text("%llu instruction bytes read; %zu retained", (unsigned long long)event.read_count, event.bytes.size());
                    for (std::size_t b = 0; b < event.bytes.size(); ++b) {
                        if (b % 16) ImGui::SameLine();
                        ImGui::Text("%02X", event.bytes[b]);
                    }
                    ImGui::EndTooltip();
                }
                ImGui::TableSetColumnIndex(3);
                if (event.kind == ObservationKind::MachineTransition) {
                    ImGui::TextDisabled("Machine transition");
                } else if (event.complete_capture) {
                    const ByteReader old_bytes = [&](uint16_t a) { return event.bytes[uint16_t(a - event.start)]; };
                    const auto ins = ctx.disasm.Decode(old_bytes, event.start, ctx.resolver(), uint32_t(event.bytes.size()));
                    ImGui::TextUnformatted(ins.text.c_str());
                } else {
                    ImGui::TextDisabled("Partial capture (no full decode)");
                }
                ImGui::TableSetColumnIndex(4);
                ImGui::Text("%04X / %llu", event.next_pc, static_cast<unsigned long long>(event.cycles));
                ImGui::TableSetColumnIndex(5);
                if (event.kind == ObservationKind::Instruction) {
                    const auto state = history.State(event, memory);
                    ImGui::TextColored(evidence_color(state), "%s", evidence_text(state));
                } else ImGui::TextDisabled("not an instruction");
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    } else {
        ImGui::TextDisabled("Observed = unchanged since execution | Modified = changed since execution");
        ImGui::TextDisabled("Unobserved = tentative decode | Not retained = older evidence dropped");
        if (rows_.empty() || layout_epoch_ != memory.ChangeEpoch() ||
            layout_generation_ != history.Generation() || layout_pc_ != pc || layout_destination_ != destination_) {
            rows_ = BuildAddressListing(memory, history, ctx.disasm, pc, destination_);
            layout_epoch_ = memory.ChangeEpoch(); layout_generation_ = history.Generation();
            layout_pc_ = pc; layout_destination_ = destination_;
        }
        if (ImGui::BeginTable("disasm", 6, ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg |
                             ImGuiTableFlags_SizingFixedFit)) {
            if (manual_scroll()) { follow_pc_ = false; scroll_target_.reset(); }
            if (follow_pc_ && pc != last_pc_) scroll_target_ = pc;
            ImGui::TableSetupColumn("BP", ImGuiTableColumnFlags_WidthFixed, 16);
            ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 84);
            ImGui::TableSetupColumn("Addr", ImGuiTableColumnFlags_WidthFixed, 48);
            ImGui::TableSetupColumn("Bytes", ImGuiTableColumnFlags_WidthFixed, 100);
            ImGui::TableSetupColumn("Instruction", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Evidence", ImGuiTableColumnFlags_WidthFixed, 94);
            const float row_height = ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().CellPadding.y * 2;
            if (scroll_target_) {
                const auto it = std::lower_bound(rows_.begin(), rows_.end(), *scroll_target_,
                    [](const AddressRow& row, uint16_t a) { return row.address < a; });
                const auto index = it - rows_.begin();
                ImGui::SetScrollY(std::max(0.0f, (float(index) - 6) * row_height));
                scroll_target_.reset();
            }
            ImGuiListClipper clipper; clipper.Begin(static_cast<int>(rows_.size()), row_height);
            bool first_visible = true;
            while (clipper.Step()) for (int line = clipper.DisplayStart; line < clipper.DisplayEnd; ++line) {
                const auto& row = rows_[std::size_t(line)];
                const uint16_t addr = row.address;
                if (first_visible) { top_ = addr; first_visible = false; }
                auto ins = ctx.disasm.Decode(ctx.reader(), addr, ctx.resolver(), row.available);
                if (row.raw) {
                    ins.mnemonic = "DB"; ins.operands.clear(); ins.symbols_used.clear(); ins.branch_target.reset();
                    ins.length = row.available;
                    for (uint32_t b = 0; b < row.available; ++b) {
                        ins.bytes[b] = memory[uint16_t(addr + b)];
                        if (b) ins.operands += ", ";
                        ins.operands += std::format("${:02X}", ins.bytes[b]);
                    }
                }
                ImGui::TableNextRow(0, row_height);
                if (addr == pc) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                    ImGui::GetColorU32(ImVec4(0.20f, 0.35f, 0.55f, 0.65f)));
                ImGui::PushID(addr);
                ImGui::TableSetColumnIndex(0);
                const bool has_bp = session.HasBreakpoint(addr);
                if (ImGui::Selectable(has_bp ? "@" : " ", false, 0, ImVec2(12, 0))) {
                    if (has_bp) session.RemoveBreakpoint(addr); else session.AddBreakpoint(addr);
                }
                ImGui::TableSetColumnIndex(1);
                if (auto sym = ctx.symbols.Lookup(addr); sym && IsCodeLabel(sym->type)) {
                    ImGui::TextColored(SymbolColor(sym->type), "%s", sym->name.c_str());
                    SymbolTooltipIfHovered(*sym);
                }
                ImGui::TableSetColumnIndex(2);
                const auto state = history.State(addr, memory);
                ImGui::TextColored(evidence_color(state), "%04X", addr);
                if (ImGui::BeginPopupContextItem("rowmenu")) {
                    if (ins.branch_target && ImGui::MenuItem("Go to target")) navigate(*ins.branch_target);
                    if (ImGui::MenuItem(has_bp ? "Remove breakpoint" : "Add breakpoint")) {
                        if (has_bp) session.RemoveBreakpoint(addr); else session.AddBreakpoint(addr);
                    }
                    ImGui::Separator();
                    if (ImGui::IsWindowAppearing()) PrimeSymbolEdit(edit_, addr, ctx.symbols);
                    DrawSymbolEditForm(ctx, edit_);
                    ImGui::EndPopup();
                }
                ImGui::TableSetColumnIndex(3);
                std::string bytes;
                for (uint32_t b = 0; b < std::min(ins.length, uint32_t{4}); ++b) bytes += std::format("{:02X} ", ins.bytes[b]);
                if (ins.length > 4) bytes += "...";
                ImGui::TextUnformatted(bytes.c_str());
                ImGui::TableSetColumnIndex(4);
                ImGui::TextUnformatted(ins.mnemonic.c_str());
                if (!ins.operands.empty()) {
                    ImGui::SameLine(); if (auto target = draw_operands(ctx, ins)) navigate(*target);
                }
                ImGui::TableSetColumnIndex(5);
                ImGui::TextColored(evidence_color(state), "%s%s", evidence_text(state), row.overlap ? " *" : "");
                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::Text("Completed executions here: %llu", (unsigned long long)history.Count(addr));
                    if (auto event = history.Latest(addr)) ImGui::Text("Latest retained observation: %llu", (unsigned long long)event->sequence);
                    if (row.overlap) ImGui::TextUnformatted("Another observed/selected start lies inside this instruction span.");
                    if (state == EvidenceState::Modified) ImGui::TextUnformatted("Current bytes are decoded here. Execution history retains the older bytes.");
                    if (state == EvidenceState::Unobserved) ImGui::TextUnformatted("This boundary is speculative; unobserved bytes are not proven data.");
                    ImGui::EndTooltip();
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        last_pc_ = pc;
    }
    ImGui::End();
}

} // namespace z80::dbg
