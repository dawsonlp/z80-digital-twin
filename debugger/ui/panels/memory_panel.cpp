//
// Z80 Digital Twin Debugger - Memory panel implementation
// Copyright (c) 2025 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//

#include "memory_panel.h"
#include "ui_context.h"
#include "symbol_style.h"

#include "imgui.h"

#include <cstdlib>
#include <string>

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

} // namespace

void MemoryPanel::Draw(UiContext& ctx) {
    ImGui::SetNextWindowPos(ImVec2(525, 120), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(760, 868), ImGuiCond_FirstUseEver);
    ImGui::Begin("Memory");

    ImGui::SetNextItemWidth(80);
    if (ImGui::InputTextWithHint("##memgoto", "addr", goto_buf_, sizeof(goto_buf_),
                                 ImGuiInputTextFlags_CharsHexadecimal |
                                 ImGuiInputTextFlags_EnterReturnsTrue)) {
        if (parse_hex16(goto_buf_, goto_addr_)) goto_pending_ = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Go")) {
        if (parse_hex16(goto_buf_, goto_addr_)) goto_pending_ = true;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("changed bytes are highlighted");

    DebugCPU& cpu = ctx.cpu();
    const auto& dirty = ctx.session.DirtyAddresses();
    const ImVec4 dirty_col(1.0f, 0.5f, 0.4f, 1.0f);

    if (ImGui::BeginChild("memscroll", ImVec2(0, 0), false,
                          ImGuiWindowFlags_HorizontalScrollbar)) {
        const float line_h = ImGui::GetTextLineHeightWithSpacing();
        if (goto_pending_) {
            ImGui::SetScrollY((goto_addr_ / 16) * line_h);
            goto_pending_ = false;
        }

        ImGuiListClipper clipper;
        clipper.Begin(4096, line_h);   // 65536 / 16 rows
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                const uint16_t base = static_cast<uint16_t>(row * 16);
                ImGui::PushID(base);
                ImGui::Text("%04X ", base);
                // Right-click the address to label this location.
                if (ImGui::BeginPopupContextItem("lbl")) {
                    if (ImGui::IsWindowAppearing()) PrimeSymbolEdit(edit_, base, ctx.symbols);
                    DrawSymbolEditForm(ctx, edit_);
                    ImGui::EndPopup();
                }
                ImGui::SameLine();
                for (int c = 0; c < 16; ++c) {
                    const uint16_t a = static_cast<uint16_t>(base + c);
                    const uint8_t v = cpu.ReadMemory(a);
                    if (dirty.count(a)) ImGui::TextColored(dirty_col, "%02X", v);
                    else                ImGui::Text("%02X", v);
                    // Hover a byte to see which symbol/region it belongs to.
                    if (ImGui::IsItemHovered())
                        if (auto sym = ctx.symbols.FindContaining(a))
                            SymbolTooltipIfHovered(*sym);
                    ImGui::SameLine();
                }
                ImGui::Text(" ");
                ImGui::SameLine();
                std::string ascii;
                for (int c = 0; c < 16; ++c) {
                    const uint8_t v = cpu.ReadMemory(static_cast<uint16_t>(base + c));
                    ascii.push_back((v >= 32 && v < 127) ? static_cast<char>(v) : '.');
                }
                ImGui::TextUnformatted(ascii.c_str());
                ImGui::PopID();
            }
        }
        clipper.End();
    }
    ImGui::EndChild();
    ImGui::End();
}

} // namespace z80::dbg
