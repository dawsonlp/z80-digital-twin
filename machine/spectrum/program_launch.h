// Standalone Spectrum RAM-program launch. No UI or assembler dependencies.
#ifndef Z80_SPECTRUM_PROGRAM_LAUNCH_H
#define Z80_SPECTRUM_PROGRAM_LAUNCH_H

#include <cstdint>
#include <span>
#include <stdexcept>

namespace z80::machine::spectrum {

struct ProgramLaunch {
    uint32_t origin;
    uint32_t entry;
    uint32_t stack;
    uint32_t stack_reserve = 256;
};

inline void ValidateProgramLaunch(std::size_t size, const ProgramLaunch& p) {
    if (size == 0 || size > 0xC000 || p.origin < 0x4000 ||
        p.origin > 0xFFFF || size > 0x10000 - p.origin)
        throw std::invalid_argument("program must fit entirely in Spectrum RAM ($4000..$FFFF)");
    const auto end = p.origin + size;
    if (p.entry < p.origin || p.entry >= end)
        throw std::invalid_argument("entry must be inside the loaded program");
    // PUSH decrements SP before storing: reserve [SP-reserve, SP).
    if (p.stack > 0xFFFF || p.stack_reserve < 2 || p.stack_reserve > p.stack ||
        p.stack - p.stack_reserve < 0x4000)
        throw std::invalid_argument("initial stack reserve must fit in writable Spectrum RAM");
    if (p.stack - p.stack_reserve < end && p.stack > p.origin)
        throw std::invalid_argument("initial stack reserve overlaps the program");
}

// Intended for a fresh, paused machine before its first frame. Host writes
// bypass CPU observers; the ULA reads the loaded memory at begin_frame().
// Validate everything before touching memory or registers.
template<class Cpu>
void LoadRamProgram(Cpu& cpu, std::span<const uint8_t> bytes, const ProgramLaunch& p) {
    ValidateProgramLaunch(bytes.size(), p);
    auto& memory = cpu.GetMemory();
    for (std::size_t i = 0; i < bytes.size(); ++i)
        if (memory.WriteProtected(static_cast<uint16_t>(p.origin + i)))
            throw std::invalid_argument("program destination is write-protected");
    for (uint32_t a = p.stack - p.stack_reserve; a < p.stack; ++a)
        if (memory.WriteProtected(static_cast<uint16_t>(a)))
            throw std::invalid_argument("initial stack reserve is write-protected");
    for (std::size_t i = 0; i < bytes.size(); ++i)
        memory.RawWrite(static_cast<uint16_t>(p.origin + i), bytes[i]);
    for (std::size_t i = 0; i < bytes.size(); ++i)
        if (cpu.ReadMemory(static_cast<uint16_t>(p.origin + i)) != bytes[i])
            throw std::runtime_error("program readback mismatch");
    cpu.Reset();
    cpu.PC() = static_cast<uint16_t>(p.entry);
    cpu.SP() = static_cast<uint16_t>(p.stack);
}

} // namespace z80::machine::spectrum
#endif
