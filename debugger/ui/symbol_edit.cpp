// Copyright (c) 2026 Larry Dawson. Licensed under the MIT License.
#include "symbol_edit.h"
#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"
#include "ui_context.h"
#include <array>
#include <format>

namespace z80::dbg {
namespace {
struct TypeOption {
    const char *label;
    SymbolType type;
};
constexpr std::array kTypes = {TypeOption{"Label", SymbolType::Label},
                               TypeOption{"Function", SymbolType::Function},
                               TypeOption{"Jump Target", SymbolType::JumpTarget},
                               TypeOption{"Byte Variable", SymbolType::ByteVariable},
                               TypeOption{"Word Variable", SymbolType::WordVariable},
                               TypeOption{"Variable", SymbolType::Variable},
                               TypeOption{"Data Region", SymbolType::DataRegion}};
int index_of(SymbolType type) {
    for (size_t i = 0; i < kTypes.size(); ++i)
        if (kTypes[i].type == type)
            return static_cast<int>(i);
    return 0;
}
} // namespace
void PrimeSymbolEdit(SymbolEditState &st, uint16_t address, const analysis::Workspace &workspace) {
    st = SymbolEditState{};
    st.address = address;
    st.original = workspace.Symbols().Lookup(address);
    if (st.original) {
        st.name = st.original->name;
        st.summary = st.original->description;
        st.type_index = index_of(st.original->type);
    }
    if (workspace.Active()) {
        const auto ids = workspace.Active()->At(address);
        st.ambiguous = ids.size() > 1;
        if (ids.size() == 1) {
            st.id = ids.front();
            const auto &record = workspace.Active()->Get().symbols.at(*st.id);
            st.extent = record.Extent().value_or(0);
        }
    }
    if (st.ambiguous)
        st.error = "Multiple interpretations at this address; select a symbol by ID with z80_analyze.";
}
void DrawSymbolEditForm(UiContext &ctx, SymbolEditState &st) {
    using namespace analysis;
    ImGui::BeginDisabled(st.original.has_value() || st.ambiguous);
    ImGui::SetNextItemWidth(100);
    ImGui::InputScalar("Address", ImGuiDataType_U16, &st.address, nullptr, nullptr, "%04X",
                       ImGuiInputTextFlags_CharsHexadecimal);
    ImGui::EndDisabled();
    if (st.id)
        ImGui::TextUnformatted("Address is fixed. Previous names remain aliases.");
    ImGui::SetNextItemWidth(260);
    ImGui::InputTextWithHint("Name", "symbol name", &st.name);
    ImGui::SetNextItemWidth(200);
    if (ImGui::BeginCombo("Type", kTypes[st.type_index].label)) {
        for (size_t i = 0; i < kTypes.size(); ++i) {
            const bool selected = st.type_index == static_cast<int>(i);
            if (ImGui::Selectable(kTypes[i].label, selected)) {
                st.type_index = static_cast<int>(i);
                if (kTypes[i].type == SymbolType::ByteVariable)
                    st.extent = 1;
                if (kTypes[i].type == SymbolType::WordVariable)
                    st.extent = 2;
            }
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::SetNextItemWidth(100);
    ImGui::InputScalar("Extent (0 = unknown)", ImGuiDataType_U32, &st.extent);
    ImGui::InputTextMultiline("Description", &st.summary, ImVec2(360, 65));
    ImGui::Separator();
    ImGui::BeginDisabled(st.name.empty() || st.ambiguous || !ctx.analysis.Active());
    if (ImGui::Button("Define")) {
        const auto extent = st.extent ? std::optional<uint32_t>(st.extent) : std::nullopt;
        Result<void> result;
        if (st.id) {
            result = ctx.analysis.Apply([&](Project &p) {
                return p.Edit(*st.id, {{Field::Name, st.name},
                                       {Field::Kind, kTypes[st.type_index].type},
                                       {Field::Summary, st.summary},
                                       {Field::Extent, extent}});
            });
        } else {
            const auto address = st.original ? st.original->address : st.address;
            if (!ctx.analysis.Active()->At(address).empty()) {
                result = std::unexpected(
                    Error{ErrorCode::Conflict, "Address already has a symbol; reopen it to edit."});
            } else {
                auto created = ctx.analysis.Apply([&](Project &p) {
                    return p.Create(p.Bind(address), st.name, kTypes[st.type_index].type, extent, st.summary);
                });
                if (!created)
                    result = std::unexpected(created.error());
            }
        }
        if (result) {
            ctx.status = std::format("Defined {}", st.name);
            ImGui::CloseCurrentPopup();
        } else
            st.error = result.error().message;
    }
    ImGui::EndDisabled();
    if (st.id) {
        ImGui::SameLine();
        if (ImGui::Button("Remove")) {
            auto result = ctx.analysis.Apply([&](Project &p) { return p.Retire(*st.id); });
            if (result) {
                ctx.status = "Retired symbol; identity and references retained";
                ImGui::CloseCurrentPopup();
            } else
                st.error = result.error().message;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
        ImGui::CloseCurrentPopup();
    if (!st.error.empty())
        ImGui::TextWrapped("%s", st.error.c_str());
}
} // namespace z80::dbg
