// Copyright (c) 2026 Larry Dawson. Licensed under the MIT License (see LICENSE).
// Bounded laboratory capture, not production debugger capture/run identity.
#include "debug_session.h"
#include "disassembler.h"
#include "transfer_analysis.h"
#include "spectrum/spectrum_machine.h"
#include "content_hash.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <format>
#include <iostream>
#include <set>
using namespace z80::dbg;
using namespace z80::dbg::analysis;
namespace {
using Kind = z80::MetadataMemory::AccessKind;
struct Case {
    std::string name, setup;
    uint16_t entry;
    unsigned budget;
    std::function<void(DebugCPU&)> configure;
    std::function<bool(const TransferSample&, DebugCPU&)> stop;
};
void save(const std::filesystem::path& path, const std::string& text) {
    std::ofstream out(path); out << text; if (!out) throw std::runtime_error("write failed: " + path.string());
}
void word(DebugCPU& cpu, uint16_t at, uint16_t value) {
    cpu.WriteMemory(at, uint8_t(value)); cpu.WriteMemory(uint16_t(at + 1), uint8_t(value >> 8));
}
}
int main(int argc, char** argv) try {
    if (argc != 3) throw std::runtime_error("usage: rom_control_flow ROM OUTPUT_DIRECTORY");
    std::ifstream input(argv[1], std::ios::binary);
    std::vector<uint8_t> rom((std::istreambuf_iterator<char>(input)), {});
    if (rom.size() != 16384) throw std::runtime_error("expected 16 KiB Spectrum ROM");
    const std::string digest = Sha256(std::string_view(reinterpret_cast<const char*>(rom.data()), rom.size()));
    if (digest != "d55daa439b673b0e3f5897f99ac37ecb45f974d1862b4dadb85dec34af99cb42")
        throw std::runtime_error("this experiment's entry addresses require the documented ROM hash");
    const std::filesystem::path folder(argv[2]); std::filesystem::create_directories(folder);
    z80::machine::spectrum::SpectrumMachine boot;
    if (!boot.load_rom(rom)) throw std::runtime_error("ROM load failed");
    for (int i = 0; i < 200; ++i) boot.run_frame();
    std::vector<uint8_t> image(65536);
    for (unsigned a = 0; a < 65536; ++a) image[a] = boot.cpu().ReadMemory(uint16_t(a));
    std::vector<Case> cases;
    for (bool runtime : {false, true}) cases.push_back({runtime ? "unstack-runtime" : "unstack-syntax",
        "Wrapper CALL $1FF5; IY=$5C3A; FLAGS bit 7 deliberately selected. Stop at $1FF8 or wrapper continuation.",
        0x1FF5, 32, [=](DebugCPU& cpu) { cpu.WriteMemory(0x5C3B, runtime ? 0x80 : 0); },
        [](const auto& s, auto&) { return s.event.next_pc == 0x1FF8 || s.event.next_pc == 0x8003; }});
    for (unsigned low = 0; low < 4; ++low) cases.push_back({"beeper-l" + std::to_string(low),
        "Wrapper CALL $03B5; HL=$0100+L; DE=1. Stop after both JP (IX) sites execute or at return. CPU-only I/O.",
        0x03B5, 1600, [=](DebugCPU& cpu) { cpu.HL() = uint16_t(0x0100 + low); cpu.DE() = 1; },
        [seen = std::set<uint16_t>{}](const auto& s, auto&) mutable {
            if (s.event.start == 0x03F0 || s.event.start == 0x03F4) seen.insert(s.event.start);
            return seen.size() == 2 || s.event.next_pc == 0x8003;
        }});
    for (bool input : {false, true}) cases.push_back({input ? "channel-input" : "channel-print",
        "Wrapper CALL printing/input entry; A=$41; CURCHL and channel records come from 200-frame boot RAM. Stop after $162C jump.",
        uint16_t(input ? 0x15E6 : 0x15F2), 40, [](auto& cpu) { cpu.A() = 0x41; },
        [](const auto& s, auto&) { return s.event.start == 0x162C; }});
    for (char channel : {'K', 'S'}) cases.push_back({channel == 'K' ? "channel-select-k" : "channel-select-s",
        "Wrapper CALL $1621; C selects K or S. Follow ROM table search, fall-through to $162C, and shared tail through $164A.",
        0x1621, 100, [=](auto& cpu) { cpu.C() = uint8_t(channel); },
        [](const auto& s, auto&) { return s.event.start == 0x164A || s.event.next_pc == 0x8003; }});
    cases.push_back({"usr-tail", "Wrapper CALL $34B6 (after argument conversion); BC=$9000; supplied RAM target contains RET. Stop at ROM continuation $2D2B.",
        0x34B6, 20, [](auto& cpu) { cpu.BC() = 0x9000; cpu.WriteMemory(0x9000, 0xC9); },
        [](const auto& s, auto&) { return s.event.next_pc == 0x2D2B; }});
    for (unsigned offset : {0, 2, 4}) cases.push_back({"calculator-offset" + std::to_string(offset),
        "Wrapper CALL $338E; L is selected byte offset into ROM word table $32D7. Exercises dispatch tail only; offset 4 follows the second RET to $3365.",
        0x338E, 30, [=](auto& cpu) { cpu.HL() = uint16_t(offset); },
        [=](const auto& s, auto&) { return s.event.start == 0x33A1 && (offset != 4 || s.event.next_pc == 0x3365); }});
    cases.push_back({"error-stack", "Wrapper CALL $0053; wrapper continuation byte=$05 as controlled error code; ERR_SP comes from boot RAM. Stop before $16C5.",
        0x0053, 20, [](auto& cpu) { cpu.WriteMemory(0x8003, 5); },
        [](const auto& s, auto&) { return s.event.next_pc == 0x16C5; }});
    cases.push_back({"clear-tail", "Wrapper CALL $1EE0; HL=$9001; word at original SP=$4567 for second POP. Stop on jump to wrapper continuation, even with changed SP.",
        0x1EE0, 24, [](auto& cpu) { cpu.HL() = 0x9001; word(cpu, 0xFF00, 0x4567); },
        [](const auto& s, auto&) { return s.event.next_pc == 0x8003; }});
    std::string manifest = "ROM SHA-256: " + digest + "\nBoot: 200 frames, RAM copied into independent CPU-only laboratory cases.\n"
        "CPU registers/machine timing are not restored from boot; no ULA/interrupt lifecycle during these captures.\n"
        "All cases use a RAM CALL wrapper at $8000, initial SP=$FF00, IY=$5C3A, and protected ROM.\n";
    for (auto& test : cases) {
        DebugCPU cpu; cpu.LoadProgram(image, 0); cpu.GetMemory().SetWriteProtect(0, 0x3FFF);
        cpu.LoadProgram({0xCD, uint8_t(test.entry), uint8_t(test.entry >> 8)}, 0x8000);
        cpu.PC() = 0x8000; cpu.SP() = 0xFF00; cpu.IY() = 0x5C3A; test.configure(cpu);
        DebugSession session(cpu);
        TransferCapture capture{"rom-lab/" + test.name + "/" + digest,
            "Controlled CPU-only ROM entry experiment, not an authentic boot-reached invocation. " + test.setup +
            " Data accesses are emulator instruction-level records, not hardware bus cycles.", {}};
        std::string listing = "; " + test.setup + "\n";
        bool stopped = false;
        for (unsigned step = 0; step < test.budget; ++step) {
            // Read values are reconstructed only when at most one read and one
            // write occurred per address. Metadata sequence establishes which
            // side of a write a read saw, including EX (SP),HL. Otherwise reject.
            struct Before { uint8_t value; uint64_t reads, writes, refused; };
            std::vector<Before> before(65536);
            auto& memory = cpu.GetMemory();
            for (unsigned a = 0; a < 65536; ++a) {
                const auto& m = memory.Metadata(uint16_t(a));
                before[a] = {cpu.ReadMemory(uint16_t(a)), m.activity[size_t(Kind::DataRead)].count,
                    m.activity[size_t(Kind::Write)].count, m.activity[size_t(Kind::Refused)].count};
            }
            TransferSample sample; sample.id = test.name + "/" + std::to_string(step);
            sample.before = {cpu.F(), cpu.B(), cpu.HL(), cpu.IX(), cpu.IY()};
            sample.stack = StackEvidence{cpu.SP(), 0, true,
                capture.samples.empty() ? std::nullopt : std::optional<std::string>(capture.samples.back().id), {}};
            const auto instruction = Disassembler{}.Decode([&](uint16_t a) { return cpu.ReadMemory(a); }, cpu.PC());
            session.StepInstruction(); sample.event = session.History().Events().back(); sample.stack->after_sp = cpu.SP();
            std::vector<std::pair<uint64_t, DataAccess>> accesses;
            for (unsigned a = 0; a < 65536; ++a) {
                const auto& m = memory.Metadata(uint16_t(a));
                const auto& r = m.activity[size_t(Kind::DataRead)]; const auto& w = m.activity[size_t(Kind::Write)];
                const auto& f = m.activity[size_t(Kind::Refused)];
                const auto nr = r.count - before[a].reads, nw = w.count - before[a].writes, nf = f.count - before[a].refused;
                if (nr > 1 || nw + nf > 1) throw std::runtime_error("ambiguous repeated data access: " + sample.id);
                if (nr) accesses.push_back({r.latest.sequence, {DataAccessKind::Read, uint16_t(a), nw && w.latest.sequence < r.latest.sequence ? w.new_value : before[a].value}});
                if (nw) accesses.push_back({w.latest.sequence, {DataAccessKind::Write, uint16_t(a), w.new_value}});
                if (nf) accesses.push_back({f.latest.sequence, {DataAccessKind::RefusedWrite, uint16_t(a), f.new_value}});
            }
            std::sort(accesses.begin(), accesses.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
            for (const auto& access : accesses) sample.stack->accesses.push_back(access.second);
            listing += std::format("{:04X}: {:24} ; -> {:04X}, SP {:04X}->{:04X}, HL {:04X}, IX {:04X}\n", sample.event.start,
                instruction.text, sample.event.next_pc, sample.stack->before_sp, sample.stack->after_sp, *sample.before.hl, *sample.before.ix);
            stopped = test.stop(sample, cpu); capture.samples.push_back(std::move(sample));
            if (stopped) break;
        }
        const auto encoded = WriteTransferCapture(capture), report = TransferReport(capture);
        if (!encoded || !report) throw std::runtime_error("invalid lab capture/report");
        save(folder / (test.name + ".capture.json"), *encoded);
        save(folder / (test.name + ".report.json"), *report);
        save(folder / (test.name + ".asm"), listing);
        manifest += test.name + ": " + std::to_string(capture.samples.size()) + " instructions, " + (stopped ? "stop condition reached" : "BUDGET EXHAUSTED") + ". " + test.setup + "\n";
        std::cout << test.name << ": " << capture.samples.size() << ", stopped=" << stopped << '\n';
    }
    save(folder / "manifest.txt", manifest);
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
