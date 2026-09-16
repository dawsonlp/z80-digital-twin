// Exercises the production ImGui form without a window-system backend.
// Copyright (c) 2026 Larry Dawson. Licensed under the MIT License.
#include "imgui.h"
#include "symbol_edit.h"
#include "ui_context.h"

#include <iostream>

using namespace z80;
using namespace z80::dbg;

int main() {
    int failures = 0;
    auto check = [&](bool ok, const char *message) {
        if (!ok) {
            std::cerr << message << '\n';
            ++failures;
        }
    };
    ImGui::CreateContext();
    auto &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {800, 600};
    io.DeltaTime = 1.0f / 60.0f;
    unsigned char *pixels = nullptr;
    int width = 0, height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

    DebugCPU cpu;
    DebugSession session(cpu);
    analysis::Workspace workspace;
    std::vector<uint8_t> image(0x2000);
    if (!workspace.Initialize(image, 0x4000))
        return 1;
    const auto &symbols = workspace.Symbols();
    auto define = [&](const Symbol &symbol) {
        auto result = workspace.Apply([&](analysis::Project &project) {
            return project.Create(project.Bind(symbol.address), symbol.name, symbol.type, symbol.size,
                                  symbol.description);
        });
        if (!result)
            throw std::runtime_error(result.error().message);
    };
    Disassembler disasm;
    DebugCommands commands;
    std::string status;
    std::optional<uint16_t> go_to;
    UiContext context{session, symbols, disasm, commands, status, go_to, workspace};
    SymbolEditState state;
    ImVec2 define_position;
    ImVec2 remove_position;
    bool popup_open = false;
    auto frame = [&](bool open = false) {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize({700, 500});
        ImGui::Begin("Test host");
        if (open)
            ImGui::OpenPopup("Symbol");
        ImGui::SetNextWindowPos({40, 40});
        if (ImGui::BeginPopup("Symbol")) {
            const float left = ImGui::GetCursorScreenPos().x;
            DrawSymbolEditForm(context, state);
            // Cancel is the last item, on the same row as Define when no error
            // is displayed. Derive the click from layout rather than pixels.
            if (state.error.empty()) {
                const auto last = ImGui::GetItemRectMin();
                define_position = {left + 10, last.y + ImGui::GetFrameHeight() / 2};
                remove_position = {last.x - ImGui::GetStyle().ItemSpacing.x -
                                       ImGui::GetStyle().FramePadding.x - ImGui::CalcTextSize("Remove").x / 2,
                                   define_position.y};
            }
            ImGui::EndPopup();
        }
        popup_open = ImGui::IsPopupOpen("Symbol");
        ImGui::End();
        ImGui::Render();
    };
    auto click = [&](ImVec2 position) {
        io.AddMousePosEvent(position.x, position.y);
        frame();
        io.AddMouseButtonEvent(0, true);
        frame();
        io.AddMouseButtonEvent(0, false);
        frame();
    };
    auto click_define = [&] { click(define_position); };
    auto open = [&](uint16_t address) {
        PrimeSymbolEdit(state, address, workspace);
        frame(true);
        frame();
    };

    const std::string long_name(160, 'N');
    define({0x4000, long_name, SymbolType::DataRegion, "Screen bytes", 6912});
    open(0x4000);
    check(state.name == long_name, "form truncated a long name");
    state.name = "DISPLAY";
    state.address = 0x9000; // Defensive check: draft address cannot relocate an edit.
    click_define();
    const auto display = symbols.Lookup(0x4000);
    check(display && display->name == "DISPLAY" && display->size == 6912 &&
              display->type == SymbolType::DataRegion && display->description == "Screen bytes",
          "saving the real form lost region metadata");
    check(!symbols.Lookup(0x9000) && symbols.Resolve(long_name) == 0x4000 && !popup_open,
          "edit relocated the symbol or failed to close");

    define({0x8000, "ENTRY", SymbolType::Label, "", 1});
    open(0x4000);
    state.name = "ENTRY";
    click_define();
    check(popup_open && !state.error.empty() && symbols.Resolve("DISPLAY") == 0x4000 &&
              symbols.Resolve("ENTRY") == 0x8000,
          "collision did not preserve symbols and show an error");
    state.name = "DISPLAY_RENAMED";
    click_define();
    check(!popup_open && symbols.Resolve("DISPLAY_RENAMED") == 0x4000 && symbols.Resolve("DISPLAY") == 0x4000,
          "corrected name could not be saved after a collision");

    define({0x5000, "STATE", SymbolType::Variable, "Generic state", 8});
    open(0x5000);
    state.name = "STATE_RENAMED";
    click_define();
    const auto generic = symbols.Lookup(0x5000);
    check(generic && generic->name == "STATE_RENAMED" && generic->type == SymbolType::Variable &&
              generic->size == 8 && generic->description == "Generic state",
          "form lost generic variable metadata");

    open(0x6000);
    state.address = 0x6100;
    state.name = "NEW";
    click_define();
    check(symbols.Resolve("NEW") == 0x6100 && !symbols.Lookup(0x6000),
          "new-symbol form did not honor its chosen address");

    open(0x5000);
    state.address = 0x8000;
    click(remove_position);
    check(!symbols.Lookup(0x5000) && !symbols.Resolve("STATE_RENAMED") && symbols.Resolve("ENTRY") == 0x8000,
          "Remove did not target the original symbol");

    open(0x6200);
    state.address = 0x8000;
    state.name = "REPLACEMENT";
    click_define();
    check(!state.error.empty() && symbols.Resolve("ENTRY") == 0x8000 && !symbols.Resolve("REPLACEMENT"),
          "new-symbol form overwrote an occupied address");

    ImGui::DestroyContext();
    return failures ? 1 : 0;
}
