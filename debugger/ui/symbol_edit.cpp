//
// Z80 Digital Twin Debugger - Symbol edit popup implementation
// Copyright (c) 2025-2026 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//

#include "symbol_edit.h"
#include "ui_context.h"

#include "imgui.h"

#include <cstdio>
#include <string>

namespace z80::dbg {
namespace {

struct TypeOption { const char* label; SymbolType type; };
constexpr TypeOption kTypes[] = {
    {"Label",         SymbolType::Label},
    {"Function",      SymbolType::Function},
    {"Jump Target",   SymbolType::JumpTarget},
    {"Byte Variable", SymbolType::ByteVariable},
    {"Word Variable", SymbolType::WordVariable},
};
constexpr int kTypeCount = static_cast<int>(sizeof(kTypes) / sizeof(kTypes[0]));

int index_of(SymbolType t) {
    for (int i = 0; i < kTypeCount; ++i)
        if (kTypes[i].type == t) return i;
    return 0;
}

uint16_t size_for(SymbolType t) {
    return t == SymbolType::WordVariable ? 2 : 1;
}

} // namespace

void PrimeSymbolEdit(SymbolEditState& st, uint16_t address, const SymbolTable& symbols) {
    st.address = address;
    if (auto sym = symbols.Lookup(address)) {
        std::snprintf(st.name, sizeof(st.name), "%s", sym->name.c_str());
        st.type_index = index_of(sym->type);
    } else {
        st.name[0] = '\0';
        st.type_index = 0;
    }
}

void DrawSymbolEditForm(UiContext& ctx, SymbolEditState& st) {
    ImGui::SetNextItemWidth(80);
    ImGui::InputScalar("Address", ImGuiDataType_U16, &st.address, nullptr, nullptr,
                       "%04X", ImGuiInputTextFlags_CharsHexadecimal);

    ImGui::SetNextItemWidth(180);
    ImGui::InputTextWithHint("Name", "symbol name", st.name, sizeof(st.name));

    ImGui::SetNextItemWidth(180);
    if (ImGui::BeginCombo("Type", kTypes[st.type_index].label)) {
        for (int i = 0; i < kTypeCount; ++i) {
            const bool selected = (i == st.type_index);
            if (ImGui::Selectable(kTypes[i].label, selected)) st.type_index = i;
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    ImGui::Separator();

    ImGui::BeginDisabled(st.name[0] == '\0');
    if (ImGui::Button("Define")) {
        const SymbolType type = kTypes[st.type_index].type;
        Symbol sym;
        sym.address = st.address;
        sym.name = st.name;
        sym.type = type;
        sym.size = size_for(type);
        ctx.symbols.Define(sym);
        ctx.status = std::string("Defined ") + st.name + " @ " +
                     [&] { char b[8]; std::snprintf(b, sizeof(b), "0x%04X", st.address); return std::string(b); }();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();

    if (ctx.symbols.Lookup(st.address)) {
        ImGui::SameLine();
        if (ImGui::Button("Remove")) {
            ctx.symbols.Remove(st.address);
            ctx.status = "Removed symbol";
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
}

} // namespace z80::dbg
