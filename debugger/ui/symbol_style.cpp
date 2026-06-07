//
// Z80 Digital Twin Debugger - Symbol styling helpers implementation
// Copyright (c) 2025-2026 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//

#include "symbol_style.h"

#include <cstdio>

namespace z80::dbg {

ImVec4 SymbolColor(SymbolType type) {
    switch (type) {
        case SymbolType::Function:     return {0.50f, 1.00f, 0.80f, 1.0f};  // teal
        case SymbolType::JumpTarget:   return {1.00f, 0.85f, 0.40f, 1.0f};  // amber
        case SymbolType::Label:        return {0.70f, 0.75f, 1.00f, 1.0f};  // blue
        case SymbolType::Variable:     return {0.90f, 0.75f, 0.50f, 1.0f};  // tan
        case SymbolType::DataRegion:   return {0.80f, 0.60f, 0.90f, 1.0f};  // violet
        case SymbolType::ByteVariable: return {1.00f, 0.60f, 0.30f, 1.0f};  // orange
        case SymbolType::WordVariable: return {0.60f, 0.55f, 1.00f, 1.0f};  // purple
    }
    return {1, 1, 1, 1};
}

bool IsCodeLabel(SymbolType type) {
    return type == SymbolType::Function ||
           type == SymbolType::Label ||
           type == SymbolType::JumpTarget;
}

void SymbolTooltipIfHovered(const Symbol& sym) {
    if (!ImGui::IsItemHovered()) return;
    ImGui::BeginTooltip();
    ImGui::TextColored(SymbolColor(sym.type), "%s", sym.name.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("(%s)", ToString(sym.type).c_str());
    char addr[16];
    std::snprintf(addr, sizeof(addr), "0x%04X", sym.address);
    ImGui::TextUnformatted(addr);
    if (sym.size > 1) {
        ImGui::SameLine();
        ImGui::TextDisabled("  size %u", sym.size);
    }
    if (!sym.description.empty()) {
        ImGui::Separator();
        ImGui::TextUnformatted(sym.description.c_str());
    }
    ImGui::EndTooltip();
}

} // namespace z80::dbg
