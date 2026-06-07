//
// Z80 Digital Twin Debugger - KeyboardPanel implementation
// Copyright (c) 2025 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//

#include "keyboard_panel.h"
#include "ui_context.h"

#include "spectrum/ula.h"

#include "imgui.h"

namespace z80::dbg {
namespace {

struct KeyCell {
    const char* label;   // primary key (also the ImGui id — must be unique)
    const char* sym;     // SYMBOL SHIFT symbol (for reference)
    uint8_t half_row;
    uint8_t bit;
};

// The 40 keys in their physical Spectrum layout, with each key's matrix position.
const KeyCell kRow1[] = {{"1","!",3,0},{"2","@",3,1},{"3","#",3,2},{"4","$",3,3},{"5","%",3,4},
                         {"6","&",4,4},{"7","'",4,3},{"8","(",4,2},{"9",")",4,1},{"0","_",4,0}};
const KeyCell kRow2[] = {{"Q","<=",2,0},{"W","<>",2,1},{"E",">=",2,2},{"R","<",2,3},{"T",">",2,4},
                         {"Y","AND",5,4},{"U","OR",5,3},{"I","AT",5,2},{"O",";",5,1},{"P","\"",5,0}};
const KeyCell kRow3[] = {{"A","~",1,0},{"S","|",1,1},{"D","\\",1,2},{"F","{",1,3},{"G","}",1,4},
                         {"H","^",6,4},{"J","-",6,3},{"K","+",6,2},{"L","=",6,1},{"ENTER","",6,0}};
const KeyCell kRow4[] = {{"CAPS","",0,0},{"Z",":",0,1},{"X","GBP",0,2},{"C","?",0,3},{"V","/",0,4},
                         {"B","*",7,4},{"N",",",7,3},{"M",".",7,2},{"SYM","",7,1},{"SPACE","",7,0}};

void draw_row(z80::machine::spectrum::Ula& ula, const KeyCell* keys, int n) {
    for (int i = 0; i < n; ++i) {
        const KeyCell& k = keys[i];
        const bool pressed = ula.matrix_pressed(k.half_row, k.bit);
        if (pressed) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.70f, 0.30f, 1.0f));
        ImGui::Button(k.label, ImVec2(58, 36));
        if (pressed) ImGui::PopStyleColor();
        if (k.sym[0] && ImGui::IsItemHovered())
            ImGui::SetTooltip("SYMBOL SHIFT + %s = %s", k.label, k.sym);
        if (i < n - 1) ImGui::SameLine();
    }
}

} // namespace

void KeyboardPanel::Draw(UiContext& /*ctx*/) {
    ImGui::SetNextWindowSize(ImVec2(620, 230), ImGuiCond_FirstUseEver);
    ImGui::Begin("Keyboard (matrix)");
    ImGui::TextDisabled("Host: Shift = CAPS SHIFT, Ctrl = SYMBOL SHIFT. Pressed keys light green.");
    ImGui::TextDisabled("Hover a key for its SYMBOL-SHIFT symbol.");
    ImGui::Spacing();
    draw_row(*ula_, kRow1, 10);
    draw_row(*ula_, kRow2, 10);
    draw_row(*ula_, kRow3, 10);
    draw_row(*ula_, kRow4, 10);
    ImGui::End();
}

} // namespace z80::dbg
