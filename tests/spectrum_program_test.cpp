#include "debug_session.h"
#include "spectrum/program_launch.h"
#include "spectrum/ula.h"

#include <fstream>
#include <iostream>
#include <vector>

using namespace z80::dbg;
namespace spectrum = z80::machine::spectrum;

int main(int argc, char** argv) {
    int failures = 0;
    auto check = [&](bool ok, const char* message) {
        if (!ok) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
    };
    DebugCPU cpu;
    DebugSession session(cpu);
    cpu.GetMemory().RawWrite(0, 0xA5);
    cpu.GetMemory().SetWriteProtect(0, 0x3FFF);
    cpu.PC() = 0x1234;
    cpu.GetMemory().RawWrite(0x8000, 0x5A);
    const std::vector<uint8_t> bytes{0xF3, 0x3E, 2, 0xD3, 0xFE, 0xC3, 5, 0x80};
    const spectrum::ProgramLaunch good{0x8000, 0x8000, 0xFF00, 256};
    for (const auto& invalid : std::vector<spectrum::ProgramLaunch>{
             {0x3FFF, 0x3FFF, 0xFF00, 256}, {0xFFFC, 0xFFFC, 0x8000, 256},
             {0x8000, 0x9000, 0xFF00, 256}, {0x8000, 0x8000, 0x8004, 256},
             {0x8000, 0x8000, 0x4001, 256}, {0x8000, 0x8000, 0x10000, 256},
             {0x8000, 0x8000, 0xFF00, 0}, {0x10000, 0x10000, 0xFF00, 256}}) {
        try { spectrum::LoadRamProgram(cpu, bytes, invalid); check(false, "invalid launch accepted"); }
        catch (const std::invalid_argument&) {}
        check(cpu.PC() == 0x1234 && cpu.ReadMemory(0x8000) == 0x5A,
              "invalid launch mutated CPU/memory");
    }
    try { spectrum::LoadRamProgram(cpu, {}, good); check(false, "empty program accepted"); }
    catch (const std::invalid_argument&) {}
    cpu.GetMemory().SetWriteProtect(0x8004, 0x8004);
    try { spectrum::LoadRamProgram(cpu, bytes, good); check(false, "protected destination accepted"); }
    catch (const std::invalid_argument&) {}
    check(cpu.ReadMemory(0x8000) == 0x5A, "protected partial load changed memory");
    cpu.GetMemory().ClearWriteProtect();
    cpu.GetMemory().SetWriteProtect(0, 0x3FFF);
    spectrum::LoadRamProgram(cpu, bytes, good);
    check(cpu.PC() == 0x8000 && cpu.SP() == 0xFF00 && !cpu.IFF1() && !cpu.IFF2(),
          "launch registers/interrupt state");
    for (std::size_t i = 0; i < bytes.size(); ++i)
        check(cpu.ReadMemory(static_cast<uint16_t>(0x8000 + i)) == bytes[i], "readback mismatch");
    check(session.DirtyAddresses().empty() && session.CoveredBytes() == 0 &&
          session.SmcCount() == 0, "host loading polluted runtime evidence");
    check(cpu.ReadMemory(0) == 0xA5 && cpu.GetMemory().WriteProtected(0), "ROM changed");
    session.AddBreakpoint(0x8005);
    session.Run();
    check(session.RunSlice(100).reason == StopReason::Breakpoint, "loaded program breakpoint");
    check(session.StepInstruction().cycles > 0, "loaded program stepping");

    // Optional real-assembler boundary used by the Python workflow test.
    if (argc == 3) {
        std::ifstream in(argv[1], std::ios::binary);
        const std::vector<uint8_t> binary(std::istreambuf_iterator<char>(in), {});
        const int color = std::stoi(argv[2]);
        DebugCPU machine_cpu;
        DebugSession machine_session(machine_cpu);
        spectrum::Ula ula;
        ula.set_clock([&] { return machine_cpu.GetCycleCount(); });
        ula.set_reader([&](uint16_t a) { return machine_cpu.ReadMemory(a); });
        machine_cpu.GetIo().inner().OnOut([&](uint16_t p, uint8_t v) { ula.write_port(p, v); });
        machine_cpu.GetMemory().AddWriteObserver(
            [&](uint16_t a, uint8_t old, uint8_t value) { ula.on_write(a, old, value); });
        spectrum::LoadRamProgram(machine_cpu, binary, good);
        ula.begin_frame();
        machine_session.RunForTStates(spectrum::timing::kTPerFrame);
        ula.end_frame();
        ula.begin_frame();
        check(ula.border() == color, "assembled program border output");
        for (uint16_t a = 0x5800; a < 0x5B00; ++a)
            check(ula.screen_byte(a, 0) == color * 8, "assembled program screen attributes");
        check(machine_cpu.PC() >= 0x8000 && machine_cpu.PC() < 0x8000 + binary.size(),
              "program remains within loaded image");
    }
    if (!failures) std::cout << "Spectrum program launch and execution passed\n";
    return failures ? 1 : 0;
}
