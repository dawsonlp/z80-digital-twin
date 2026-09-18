// Copyright (c) 2026 Larry Dawson. Licensed under the MIT License.
#include "debugger_app.h"
#include "content_hash.h"
#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"
#include <fstream>
#include <format>

namespace z80::dbg {

void DebuggerApp::ForgetLivePatch() {
    patch_plan_.reset();
    patch_analysis_.reset();
    live_patch_.reset();
}

void DebuggerApp::EnsureLivePatch() {
    if (live_patch_) return;
    live_patch_ = std::make_unique<LivePatch>(*session_, [this] {
        auto analysis = std::make_shared<const analysis::Workspace>(analysis_);
        const auto revision = analysis_.CurrentRevision();
        const auto analysis_path = analysis_path_;
        const auto sym_path = sym_path_;
        const auto beeper = beeper_;
        using Devices = machine::spectrum::DebugSpectrumMachine::DeviceSnapshot;
        auto devices = spectrum_ ? std::make_shared<const Devices>(spectrum_->CaptureDevices()) : nullptr;
        return LivePatch::Devices{
            [this, revision, devices] {
                return analysis_.CurrentRevision() == revision &&
                    (!devices || spectrum_->CaptureDevices() == *devices);
            },
            [this, analysis, devices, beeper, analysis_path, sym_path] {
                // Prepare every allocating copy before restoring any component.
                auto restored_analysis = *analysis;
                auto restored_analysis_path = analysis_path;
                auto restored_sym_path = sym_path;
                auto restored_devices = devices ? std::optional<Devices>(*devices) : std::nullopt;
                if (restored_devices) spectrum_->RestoreDevices(std::move(*restored_devices));
                analysis_ = std::move(restored_analysis);
                analysis_path_.swap(restored_analysis_path);
                sym_path_.swap(restored_sym_path);
                beeper_ = beeper;
                audio_samples_.clear();
                paced_ = false;
                frame_accum_ = 0;
            }};
    });
}

void DebuggerApp::DrawLivePatch() {
    if (!show_live_patch_) return;
    EnsureLivePatch();
    ImGui::SetNextWindowPos(ImVec2(300, 140), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(720, 640), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Live code update", &show_live_patch_)) { ImGui::End(); return; }
    const bool running = spectrum_running_ || session_->State() == RunState::Running;
    ImGui::TextWrapped("In-process checkpoint and raw binary replacement. State is retained. "
        "This first increment uses explicit state edits; automatic relocation suggestions and editor attachment are pending.");
    if (ImGui::Button("Pause")) {
        session_->Pause(); spectrum_running_ = false;
        status_ = "Paused for live update";
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(running);
    if (ImGui::Button("Save checkpoint")) {
        const auto result = live_patch_->SaveCheckpoint();
        status_ = result ? "Checkpoint saved in this process (not on disk)" : result.error().message;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!live_patch_->HasCheckpoint());
    if (ImGui::Button("Restore checkpoint")) {
        const auto result = live_patch_->RestoreCheckpoint();
        status_ = result ? "Checkpoint restored; paused; transient analysis begins a new epoch" : result.error().message;
        if (result) {
            patch_plan_.reset(); patch_analysis_.reset();
            disasm_goto_ = session_->Cpu().PC();
            if (!audio_.clear()) status_ += "; audio queue reset failed";
        }
    }
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    ImGui::Separator();
    ImGui::InputText("Assembled binary", &patch_path_);
    ImGui::InputScalar("Load address", ImGuiDataType_U16, &patch_origin_, nullptr, nullptr, "%04X",
                       ImGuiInputTextFlags_CharsHexadecimal);
    ImGui::TextWrapped("The entire selected binary range will be written, including data. "
        "Review overwritten live values below. Vacated bytes outside this range are retained.");
    ImGui::Checkbox("Set PC", &patch_pc_enabled_); ImGui::SameLine();
    ImGui::InputScalar("##patch-pc", ImGuiDataType_U16, &patch_pc_, nullptr, nullptr, "%04X", ImGuiInputTextFlags_CharsHexadecimal);
    ImGui::Checkbox("Set HL", &patch_hl_enabled_); ImGui::SameLine();
    ImGui::InputScalar("##patch-hl", ImGuiDataType_U16, &patch_hl_, nullptr, nullptr, "%04X", ImGuiInputTextFlags_CharsHexadecimal);
    ImGui::Checkbox("Set memory word (e.g. saved return address)", &patch_stack_enabled_);
    ImGui::InputScalar("Word address", ImGuiDataType_U16, &patch_stack_address_, nullptr, nullptr, "%04X", ImGuiInputTextFlags_CharsHexadecimal);
    ImGui::InputScalar("Word value", ImGuiDataType_U16, &patch_stack_value_, nullptr, nullptr, "%04X", ImGuiInputTextFlags_CharsHexadecimal);
    ImGui::TextDisabled("Word changes do not change SP. Other registers can be edited in Registers while paused.");
    ImGui::BeginDisabled(running);
    if (ImGui::Button("Review update")) {
        patch_plan_.reset(); patch_analysis_.reset();
        try {
            std::vector<LivePatch::Range> ranges;
            if (!patch_path_.empty()) {
                std::ifstream input(patch_path_, std::ios::binary | std::ios::ate);
                if (!input) throw std::runtime_error("Cannot open assembled binary");
                const auto size = input.tellg();
                if (size <= 0 || size > 65536 - uint32_t(patch_origin_))
                    throw std::runtime_error("Binary is empty or does not fit at the load address");
                std::vector<uint8_t> bytes(static_cast<size_t>(size));
                input.seekg(0);
                if (!input.read(reinterpret_cast<char*>(bytes.data()), size))
                    throw std::runtime_error("Failed reading assembled binary");
                patch_hash_ = Sha256(bytes);
                analysis::Workspace next;
                if (auto result = next.Initialize(bytes, patch_origin_); !result)
                    throw std::runtime_error(result.error().message);
                patch_analysis_ = std::move(next);
                ranges.push_back({patch_origin_, std::move(bytes)});
            } else patch_hash_ = "State-only update";
            if (patch_stack_enabled_)
                ranges.push_back({patch_stack_address_, {uint8_t(patch_stack_value_), uint8_t(patch_stack_value_ >> 8)}});
            auto state = session_->Cpu().CaptureState();
            if (!state) throw std::runtime_error("Complete the pending instruction before review");
            if (patch_pc_enabled_) state->pc = patch_pc_;
            if (patch_hl_enabled_) state->hl = patch_hl_;
            auto result = live_patch_->Prepare(std::move(ranges), state);
            if (!result) throw std::runtime_error(result.error().message);
            patch_plan_ = std::move(*result);
            status_ = "Review captured; no writes applied";
        } catch (const std::exception& e) { status_ = e.what(); }
    }
    ImGui::EndDisabled();

    if (patch_plan_) {
        ImGui::Separator();
        ImGui::TextWrapped("Reviewed bytes: %s", patch_hash_.c_str());
        const auto& before = patch_plan_->Before();
        const auto& after = patch_plan_->After();
        ImGui::Text("PC %04X -> %04X   HL %04X -> %04X   SP %04X -> %04X",
                    before.pc, after.pc, before.hl, after.hl, before.sp, after.sp);
        ImGui::TextWrapped("Apply uses this captured review, not subsequent form or file edits. "
            "For changed inputs, review again. Binary replacement starts a new image-bound symbol project; "
            "the checkpoint retains the old analysis. Save valuable annotations to disk first.");
        struct Row { uint16_t address; uint8_t before, after; };
        std::vector<Row> rows;
        for (const auto& range : patch_plan_->Ranges())
            for (uint32_t i = 0; i < range.bytes.size(); ++i) {
                const auto address = uint16_t(range.address + i);
                const auto old = patch_plan_->BeforeByte(address);
                if (old != range.bytes[i]) rows.push_back({address, old, range.bytes[i]});
            }
        ImGui::Text("%zu changed bytes; machine time remains %llu", rows.size(),
                    static_cast<unsigned long long>(before.cycles));
        ImGui::BeginChild("Changed bytes", ImVec2(0, 130), true);
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(rows.size()));
        while (clipper.Step())
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                const auto& row = rows[size_t(i)];
                ImGui::Text("%04X: %02X -> %02X", row.address, row.before, row.after);
            }
        ImGui::EndChild();
        ImGui::BeginDisabled(running);
        if (ImGui::Button("Save checkpoint and apply reviewed update")) {
            auto result = live_patch_->Apply(*patch_plan_);
            status_ = result ? "Update applied; paused; prior code and state retained in checkpoint" : result.error().message;
            if (result) {
                if (patch_analysis_) {
                    analysis_ = std::move(*patch_analysis_);
                    analysis_path_.clear(); sym_path_.clear();
                }
                disasm_goto_ = session_->Cpu().PC();
                patch_plan_.reset(); patch_analysis_.reset();
            } else if (result.error().code == LivePatch::ErrorCode::Failed ||
                       result.error().code == LivePatch::ErrorCode::RestoreFailed) {
                if (!audio_.clear()) status_ += "; audio queue reset failed";
            }
        }
        ImGui::EndDisabled();
    }
    ImGui::TextWrapped("%s", status_.c_str());
    ImGui::End();
}
} // namespace z80::dbg
