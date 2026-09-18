// Copyright (c) 2026 Larry Dawson. Licensed under the MIT License.
#include "live_patch.h"
#include "spectrum/spectrum_machine.h"
#include <iostream>
#include <fstream>
#include <stdexcept>

using namespace z80;
using namespace z80::dbg;
namespace sm = z80::machine::spectrum;
namespace {
int failures = 0;
void check(bool value, const char* text) {
    std::cout << (value ? "PASS " : "FAIL ") << text << '\n';
    if (!value) ++failures;
}
void require(bool value, const char* text) {
    check(value, text);
    if (!value) throw std::runtime_error(text);
}
auto devices(sm::DebugSpectrumMachine& machine) {
    auto state = std::make_shared<const sm::DebugSpectrumMachine::DeviceSnapshot>(machine.CaptureDevices());
    return LivePatch::Devices{
        [&machine, state] { return machine.CaptureDevices() == *state; },
        [&machine, state] { machine.RestoreDevices(*state); }};
}

void cpu_state() {
    auto cpu = std::make_unique<DebugCPU>();
    cpu->LoadProgram({0xFB, 0x00, 0x76}, 0x8000); // EI, NOP, HALT
    cpu->PC() = 0x8000; cpu->SP() = 0xFF00;
    cpu->AltAF() = 0x1234; cpu->AltBC() = 0x2345; cpu->AltDE() = 0x3456;
    cpu->AltHL() = 0x4567; cpu->WZ() = 0x5678; cpu->IX() = 0x6789;
    cpu->StepInstruction();
    const auto after_ei = cpu->CaptureState();
    require(bool(after_ei), "CPU captures complete instruction boundary");
    check(after_ei->ei_defer, "checkpoint includes EI deferral");
    cpu->StepInstruction();
    check(cpu->Interrupt(), "interrupt accepted after EI shadow expires");
    require(cpu->RestoreState(*after_ei), "CPU restores state without memory reset");
    check(cpu->CaptureState() == after_ei && !cpu->Interrupt(), "restored EI shadow still rejects interrupt");
    cpu->StepInstruction(); cpu->StepInstruction();
    const auto halted = cpu->CaptureState();
    check(halted->halted, "HALT latch captured");
    cpu->SetHalted(false); cpu->AltAF() = 0;
    require(cpu->RestoreState(*halted), "restore halted CPU");
    check(cpu->IsHalted() && cpu->AltAF() == 0x1234, "HALT and alternate registers restored");
    auto invalid = *halted; invalid.interrupt_mode = 3;
    check(!cpu->RestoreState(invalid) && cpu->CaptureState() == halted, "invalid state rejected without mutation");
    cpu->SetHalted(false); cpu->PC() = 0x9000; cpu->WriteMemory(0x9000, 0xDD);
    cpu->Step();
    check(!cpu->CaptureState() && !cpu->RestoreState(*halted), "partial prefix cannot be captured or overwritten");
}

void replacement(std::vector<uint8_t> old = {}, std::vector<uint8_t> next = {}) {
    auto cpu = std::make_unique<DebugCPU>();
    auto session = std::make_unique<DebugSession>(*cpu);
    LivePatch live(*session);
    if (old.empty()) {
        old.resize(18, 0);
        old[0] = 0xCD; old[1] = 0x10; old[2] = 0x80; old[3] = 0x76;
        old[17] = 0xC9;
    }
    cpu->LoadProgram(old, 0x8000); cpu->PC() = 0x8000; cpu->SP() = 0xFF00;
    session->StepInstruction(); // actual CALL creates live return word
    check(cpu->PC() == 0x8010 && cpu->SP() == 0xFEFE && cpu->ReadMemory(0xFEFE) == 3,
          "actual CALL establishes saved continuation");
    cpu->WriteMemory(0xA000, 237);
    const auto before = *cpu->CaptureState();
    auto memory = std::make_unique<MetadataMemory::Snapshot>(cpu->GetMemory().CaptureState());
    if (next.empty()) {
        next.resize(20, 0);
        next[1] = 0xCD; next[2] = 0x12; next[3] = 0x80; next[4] = 0x76;
        next[19] = 0xC9;
    }
    auto state = before; state.pc = 0x8012; state.hl = 0x9020;
    auto plan = live.Prepare({{0x8000, next}, {0xFEFE, {0x04, 0x80}}}, state);
    require(bool(plan), "review accepts changed-size image and explicit state choices");
    check(cpu->CaptureState() == before && cpu->GetMemory().CaptureState() == *memory,
          "review is read-only");
    require(bool(live.Apply(*plan)), "apply code and approved continuation/state edits");
    check(cpu->SP() == before.sp && cpu->GetCycleCount() == before.cycles && cpu->ReadMemory(0xA000) == 237,
          "apply preserves SP, clock and unrelated live data");
    check(session->State() == RunState::Paused && session->SmcCount() == 0,
          "host patch remains paused and is not CPU self modification");
    check(!live.Apply(*plan), "already consumed review cannot apply twice");
    session->StepInstruction(); session->StepInstruction();
    check(cpu->PC() == 0x8004 && cpu->SP() == 0xFF00 && cpu->HL() == 0x9020,
          "new code returns through adjusted stack word with scenario HL");
    require(bool(live.RestoreCheckpoint()), "restore checkpoint after executing replacement");
    check(cpu->CaptureState() == before && cpu->GetMemory().CaptureState() == *memory,
          "restore recovers exact old code and CPU/RAM state");
    // Keeping the old return word is a valid explicit experiment, not forbidden.
    auto kept = live.Prepare({{0x8000, next}}, state);
    require(bool(kept) && bool(live.Apply(*kept)), "replacement permits retaining old continuation");
    check(cpu->ReadMemory(0xFEFE) == 3, "declined adjustment is not silently applied");
}

void validation() {
    auto cpu = std::make_unique<DebugCPU>();
    auto session = std::make_unique<DebugSession>(*cpu);
    LivePatch live(*session);
    cpu->PC() = 0x8000;
    auto plan = live.Prepare({{0x8000, {0x00, 0x76}}});
    require(bool(plan), "prepare validation fixture");
    cpu->HL() = 1;
    check(!live.Apply(*plan) && cpu->ReadMemory(0x8001) == 0, "register edit invalidates review without writes");
    cpu->HL() = 0;
    cpu->WriteMemory(0x9000, 1); cpu->WriteMemory(0x9000, 0);
    check(!live.Apply(*plan), "write-and-restore memory also invalidates review");
    cpu->GetMemory().SetWriteProtect(0, 0x3FFF);
    check(!live.Prepare({{0x8000, {1}}, {0x3FFF, {2, 3}}}) && cpu->ReadMemory(0x8000) == 0,
          "mixed protected range fails before any write");
    check(!live.Prepare({{0xFFFF, {1, 2}}}), "host writes cannot wrap address space");
    check(!live.Prepare({{0x8000, {1, 2}}, {0x8001, {3}}}), "overlap requires explicit disposition");
    check(!live.Prepare({{0x8000, {}}}), "empty byte range rejected");
    session->Run();
    check(!live.Prepare({}) && !live.SaveCheckpoint(), "running session rejects mutation/checkpoint");
    session->Pause();
    cpu->WriteMemory(0x8000, 0xDD);
    session->StepInstruction(1);
    check(!live.SaveCheckpoint() && !live.Prepare({}), "partial instruction cannot be patched");
}

void machine_restore() {
    auto machine = std::make_unique<sm::DebugSpectrumMachine>();
    auto& cpu = machine->cpu();
    auto session = std::make_unique<DebugSession>(cpu);
    session->SetExecutionHooks([&] { machine->prepare_execution(); }, [&] { machine->advance_execution(); });
    LivePatch live(*session, [&] { return devices(*machine); });
    // Change screen, border and beeper repeatedly in a partial frame.
    cpu.LoadProgram({0x21, 0x00, 0x40, 0x34, 0x7E, 0xD3, 0xFE, 0x18, 0xFA}, 0x8000);
    cpu.PC() = 0x8000; cpu.SP() = 0xFF00;
    machine->set_rom_write_protect(true);
    const std::vector<uint8_t> tape{2, 0, 0, 0};
    require(machine->load_tape(tape), "load checkpoint tape fixture");
    machine->play_tape();
    machine->ula().key_down(1, 2);
    for (int i = 0; i < 173; ++i) session->StepInstruction();
    (void)machine->tape().ear_level(cpu.GetCycleCount());
    session->AddBreakpoint(0xABCD); session->AddWatchpoint(0xA001);
    const auto cpu_before = *cpu.CaptureState();
    auto mem_before = std::make_unique<MetadataMemory::Snapshot>(cpu.GetMemory().CaptureState());
    auto device_before = std::make_unique<sm::DebugSpectrumMachine::DeviceSnapshot>(machine->CaptureDevices());
    require(bool(live.SaveCheckpoint()), "save partial-frame machine and tape state");
    std::vector<std::pair<uint64_t, uint8_t>> edges;
    machine->on_frame_completed([&] {
        for (const auto& edge : machine->ula().beeper_edges()) edges.emplace_back(edge.cycle, edge.level);
    });
    for (int i = 0; i < 10000; ++i) session->StepInstruction();
    (void)machine->tape().ear_level(cpu.GetCycleCount());
    const auto future_cpu = *cpu.CaptureState();
    auto future_mem = std::make_unique<MetadataMemory::Snapshot>(cpu.GetMemory().CaptureState());
    auto future_devices = std::make_unique<sm::DebugSpectrumMachine::DeviceSnapshot>(machine->CaptureDevices());
    const auto future_edges = edges;
    session->ClearBreakpoints(); session->RemoveWatchpoint(0xA001);
    machine->stop_tape(); machine->ula().release_all_keys();
    cpu.GetMemory().ClearWriteProtect();
    require(bool(live.RestoreCheckpoint()), "restore full machine at partial frame");
    check(cpu.CaptureState() == cpu_before && cpu.GetMemory().CaptureState() == *mem_before &&
          machine->CaptureDevices() == *device_before, "CPU/RAM/protection/keyboard/tape/raster state restored");
    check(session->HasBreakpoint(0xABCD) && session->Watchpoints() == std::vector<uint16_t>{0xA001},
          "checkpoint restores debugger control definitions");
    edges.clear();
    for (int i = 0; i < 10000; ++i) session->StepInstruction();
    (void)machine->tape().ear_level(cpu.GetCycleCount());
    check(cpu.CaptureState() == future_cpu && cpu.GetMemory().CaptureState() == *future_mem &&
          machine->CaptureDevices() == *future_devices && edges == future_edges && !edges.empty(),
          "restored continuation matches uninterrupted CPU, memory, raster, tape and beeper edges");

    auto stale = live.Prepare({{0xA000, {1}}});
    require(bool(stale), "review captures device context");
    machine->ula().key_up(1, 2);
    check(!live.Apply(*stale), "device edit invalidates paused review");

    const auto prior_cpu = *cpu.CaptureState();
    auto prior_mem = std::make_unique<MetadataMemory::Snapshot>(cpu.GetMemory().CaptureState());
    auto prior_devices = std::make_unique<sm::DebugSpectrumMachine::DeviceSnapshot>(machine->CaptureDevices());
    auto patch = live.Prepare({{0x4000, {0xE1, 0xE2}}});
    require(bool(patch), "prepare display-memory patch");
    const int observer = cpu.GetMemory().AddWriteObserver([](uint16_t address, uint8_t, uint8_t) {
        if (address == 0x4001) throw std::runtime_error("injected observer failure");
    });
    const auto failure = live.Apply(*patch);
    check(!failure && failure.error().code == LivePatch::ErrorCode::Failed,
          "injected mid-write failure is reported");
    check(cpu.CaptureState() == prior_cpu && cpu.GetMemory().CaptureState() == *prior_mem &&
          machine->CaptureDevices() == *prior_devices && session->State() == RunState::Paused,
          "failed write rolls back CPU, bytes and partial raster effects while staying paused");
    cpu.GetMemory().RemoveWriteObserver(observer);
    auto success = live.Prepare({{0x4000, {0xE1, 0xE2}}});
    require(bool(success) && bool(live.Apply(*success)), "apply display patch after fault removed");
    check(cpu.GetCycleCount() == prior_cpu.cycles && session->SmcCount() == 0 &&
          machine->CaptureDevices() != *prior_devices,
          "host display writes update device timeline without advancing clock or recording CPU SMC");
}

void failed_recovery() {
    auto cpu = std::make_unique<DebugCPU>();
    auto session = std::make_unique<DebugSession>(*cpu);
    bool fail_restore = true;
    LivePatch live(*session, [&] {
        return LivePatch::Devices{[] { return true; }, [&] {
            if (fail_restore) throw std::runtime_error("injected restoration failure");
        }};
    });
    auto plan = live.Prepare({{0x8000, {0x3E, 0x42}}});
    require(bool(plan), "prepare rollback failure fixture");
    auto observer = cpu->GetMemory().AddWriteObserver([](uint16_t, uint8_t, uint8_t) {
        throw std::runtime_error("injected write failure");
    });
    const auto result = live.Apply(*plan);
    check(!result && result.error().code == LivePatch::ErrorCode::RestoreFailed && session->RecoveryRequired(),
          "failed rollback latches recovery requirement");
    session->Run();
    const auto state = *cpu->CaptureState();
    check(session->State() == RunState::Paused &&
          session->StepInstruction().reason == StopReason::RecoveryRequired &&
          session->StepOver().reason == StopReason::RecoveryRequired &&
          session->RunSlice(10).reason == StopReason::RecoveryRequired &&
          session->RunForTStates(100).reason == StopReason::RecoveryRequired && cpu->CaptureState() == state,
          "all execution entry points refuse partially restored state");
    check(!live.SaveCheckpoint() && !live.Prepare({}) && !live.Apply(*plan),
          "failed state cannot replace the recovery checkpoint or accept another patch");
    cpu->GetMemory().RemoveWriteObserver(observer);
    fail_restore = false;
    require(bool(live.RestoreCheckpoint()), "recovery retry restores retained checkpoint");
    check(!session->RecoveryRequired() && cpu->ReadMemory(0x8000) == 0,
          "verified recovery re-enables stepping with old bytes");
    auto retry = live.Prepare({{0x8000, {0x3E}}});
    require(bool(retry), "prepare non-standard callback exception fixture");
    observer = cpu->GetMemory().AddWriteObserver([](uint16_t, uint8_t, uint8_t) { throw 42; });
    const auto unusual = live.Apply(*retry);
    check(!unusual && unusual.error().code == LivePatch::ErrorCode::Failed && cpu->ReadMemory(0x8000) == 0,
          "non-standard callback exception also rolls back");
    cpu->GetMemory().RemoveWriteObserver(observer);
}
}
int main(int argc, char** argv) {
    try {
        if (argc == 3) {
            auto read = [](const char* path) {
                std::ifstream input(path, std::ios::binary);
                if (!input) throw std::runtime_error("Cannot read assembled fixture");
                std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(input), {}};
                if (bytes.empty()) throw std::runtime_error("Empty assembled fixture");
                return bytes;
            };
            replacement(read(argv[1]), read(argv[2]));
        } else {
            cpu_state(); replacement(); validation(); machine_restore(); failed_recovery();
        }
    }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
    return failures ? 1 : 0;
}
