#include "debug_session.h"
#include "address_listing.h"
#include <algorithm>
#include <iostream>
#include <vector>

using namespace z80::dbg;
int failures = 0;
void check(bool ok, const char* name) {
    if (!ok) { ++failures; std::cerr << "FAIL: " << name << '\n'; }
}
bool has_row(const std::vector<AddressRow>& rows, uint16_t address) {
    return std::any_of(rows.begin(), rows.end(), [=](auto row) { return row.address == address; });
}
int main() {
    {
        DebugCPU cpu; DebugSession session(cpu);
        cpu.LoadProgram({0x3E, 2, 0x3A, 0x00, 0x90}, 0x8000);
        cpu.PC() = 0x8000; cpu.WriteMemory(0x9000, 0xA5);
        session.StepInstruction();
        const auto old = *session.History().Latest(0x8000);
        check(old.bytes == std::vector<uint8_t>({0x3E, 2}) && old.next_pc == 0x8002 && old.cycles == 7,
              "exact original bytes, successor and cycles captured");
        check(session.History().State(old, cpu.GetMemory()) == EvidenceState::Observed, "fresh observation valid");
        cpu.WriteMemory(0x8001, 2);
        check(session.History().State(old, cpu.GetMemory()) == EvidenceState::Observed, "same-value write retains validity");
        cpu.GetMemory().SetWriteProtect(0x8000, 0x8001); cpu.WriteMemory(0x8001, 4);
        check(session.History().State(old, cpu.GetMemory()) == EvidenceState::Observed, "refused write retains validity");
        cpu.GetMemory().ClearWriteProtect(); cpu.WriteMemory(0x8001, 4);
        check(session.History().State(old, cpu.GetMemory()) == EvidenceState::Modified, "operand change invalidates entire instruction");
        cpu.WriteMemory(0x8001, 2);
        check(session.History().State(old, cpu.GetMemory()) == EvidenceState::Modified, "changed then restored remains modified since observation");
        cpu.GetMemory().RawWrite(0x8001, 4); cpu.PC() = 0x8000; session.StepInstruction();
        check(old.bytes[1] == 2 && session.History().Latest(0x8000)->bytes[1] == 4 &&
              session.History().Count(0x8000) == 2, "new execution preserves old version and establishes new one");
        session.StepInstruction();
        check(session.History().Latest(0x8002)->bytes == std::vector<uint8_t>({0x3A, 0, 0x90}),
              "data read is excluded from instruction bytes");
        check(cpu.A() == 0xA5, "capturing does not change the executed load");
        const auto newer = *session.History().Latest(0x8000);
        cpu.GetMemory().RawWrite(0x8000, 0);
        check(session.History().State(newer, cpu.GetMemory()) == EvidenceState::Modified, "host RawWrite invalidates evidence");
        const auto rows = BuildAddressListing(cpu.GetMemory(), session.History(), Disassembler{}, cpu.PC(), 0x8000);
        check(rows.front().address == 0 && has_row(rows, 0x8000) && has_row(rows, 0x8002) &&
              rows.back().address > cpu.PC(), "listing includes addresses before and after PC and modified starts");
        session.Reset();
        check(session.History().Events().empty() && session.History().Count(0x8000) == 0,
              "reset clears observations and counts");
    }
    {
        DebugCPU cpu; DebugSession session(cpu);
        cpu.LoadProgram({0x32, 0x01, 0x80}, 0x8000); cpu.PC() = 0x8000; cpu.A() = 0x42;
        session.StepInstruction();
        const auto& event = *session.History().Latest(0x8000);
        check(event.bytes == std::vector<uint8_t>({0x32, 1, 0x80}) && cpu.ReadMemory(0x8001) == 0x42,
              "self-overwrite preserves bytes read before the write");
        check(session.History().State(event, cpu.GetMemory()) == EvidenceState::Modified,
              "self-overwriting instruction is immediately modified");
    }
    {
        DebugCPU cpu; DebugSession session(cpu);
        cpu.LoadProgram({0x21, 0x00, 0x00}, 0x8000); cpu.PC() = 0x8000;
        session.StepInstruction(); cpu.PC() = 0x8001; session.StepInstruction();
        const auto rows = BuildAddressListing(cpu.GetMemory(), session.History(), Disassembler{}, 0x8002, 0x8000);
        check(has_row(rows, 0x8000) && has_row(rows, 0x8001), "overlapping observed starts remain visible");
        auto it = std::find_if(rows.begin(), rows.end(), [](auto r) { return r.address == 0x8000; });
        check(it != rows.end() && it->overlap, "overlapping row is explicit");
    }
    {
        DebugCPU cpu; DebugSession session(cpu);
        cpu.GetMemory().RawWrite(0xFFFF, 0x21); cpu.GetMemory().RawWrite(0, 0x34); cpu.GetMemory().RawWrite(1, 0x12);
        cpu.PC() = 0xFFFF; session.StepInstruction();
        const auto old = *session.History().Latest(0xFFFF);
        check(old.complete_capture && old.bytes == std::vector<uint8_t>({0x21, 0x34, 0x12}) && cpu.HL() == 0x1234,
              "wrapped instruction captured in address-stream order");
        cpu.WriteMemory(0, 0x35);
        check(session.History().State(old, cpu.GetMemory()) == EvidenceState::Modified, "wrapped operand invalidates start at FFFF");
    }
    {
        DebugCPU cpu; DebugSession session(cpu);
        std::vector<uint8_t> code(20, 0xDD); code.push_back(0x00);
        cpu.LoadProgram(code, 0x8000); cpu.PC() = 0x8000;
        session.StepInstruction(8);
        check(session.History().Events().empty(), "prefix budget does not publish a completed instruction");
        session.StepInstruction();
        check(session.History().Latest(0x8000)->bytes == code && session.History().Latest(0x8000)->cycles == 84,
              "prefix continuation retains all bytes and total instruction time");
        session.Reset(); code.assign(300, 0xDD); code.push_back(0x00);
        cpu.LoadProgram(code, 0); session.StepInstruction();
        const auto* event = session.History().Latest(0);
        check(event && !event->complete_capture && event->bytes.size() == 256 && event->read_count == 301,
              "capture limit is explicit and bounded");
        check(session.History().Anchors(cpu.GetMemory()).empty(), "partial capture cannot establish a reliable span");
    }
    {
        DebugCPU cpu; DebugSession session(cpu);
        session.RunSlice(InstructionHistory::kCapacity + 2);
        check(session.History().Events().size() == InstructionHistory::kCapacity && session.History().Dropped() == 2,
              "bounded history reports evictions");
        check(session.History().State(0, cpu.GetMemory()) == EvidenceState::NotRetained &&
              session.History().Latest(2) != nullptr, "eviction removes dangling anchors but retains execution count");
    }
    {
        DebugCPU cpu; DebugSession session(cpu);
        cpu.LoadProgram({0xDD, 0xDD, 0x21, 0x34, 0x12}, 0x8000); cpu.PC() = 0x8000;
        session.StepInstruction(1);
        cpu.GetMemory().RawWrite(0x8000, 0xFD);
        session.StepInstruction();
        const auto* event = session.History().Latest(0x8000);
        check(event && event->bytes == std::vector<uint8_t>({0xDD, 0xDD, 0x21, 0x34, 0x12}) &&
              session.History().State(*event, cpu.GetMemory()) == EvidenceState::Modified,
              "prefix changes during a pause preserve the originally consumed prefix");
    }
    {
        DebugCPU cpu; DebugSession session(cpu);
        bool prepared = false;
        session.SetExecutionHooks([&] {
            if (!prepared) { cpu.IFF1() = true; cpu.Interrupt(); prepared = true; }
        }, [] {});
        session.StepInstruction();
        const auto& events = session.History().Events();
        check(events.size() == 2 && events[0].kind == ObservationKind::MachineTransition &&
              events[0].bytes.empty() && events[0].next_pc == 0x38 && events[0].cycles == 13 &&
              events[1].kind == ObservationKind::Instruction && events[1].start == 0x38 &&
              events[1].bytes == std::vector<uint8_t>({0}) && events[1].cycles == 4,
              "machine entry is a separate transition without fabricated instruction bytes");
    }
    {
        DebugCPU cpu; DebugSession session(cpu);
        cpu.LoadProgram({0x21, 0x11, 0x22}, 0);
        const auto rows = BuildAddressListing(cpu.GetMemory(), session.History(), Disassembler{}, 1, 0xFFFF);
        check(rows.front().raw && rows.front().available == 1 && has_row(rows, 1) && has_row(rows, 0xFFFF),
              "a tentative decode cannot swallow PC or the final address");
        for (uint32_t a = 0; a < 65536; ++a) cpu.GetMemory().RawWrite(a, 0xDD);
        const auto prefixes = BuildAddressListing(cpu.GetMemory(), session.History(), Disassembler{}, 0x8000, 0);
        check(prefixes.front().raw && has_row(prefixes, 0x8000) && prefixes.back().address >= 0xFFFC,
              "all-prefix memory browsing remains bounded and reaches both ends");
    }

    // Cover capture sites across opcode families. Decoder agreement is a
    // consistency check, not an independent hardware-conformance oracle.
    for (const std::vector<uint8_t>& prefix : std::vector<std::vector<uint8_t>>{{}, {0xCB}, {0xED}, {0xDD}, {0xFD}, {0xDD,0xCB,1}, {0xFD,0xCB,0xFF}}) {
        for (unsigned op = 0; op < 256; ++op) {
            DebugCPU cpu; DebugSession session(cpu);
            auto code = prefix; code.push_back(uint8_t(op)); code.insert(code.end(), 8, 0);
            cpu.LoadProgram(code, 0x8000); cpu.PC() = 0x8000; cpu.SP() = 0xF000; cpu.BC() = 1;
            const auto decoded = Disassembler{}.Decode([&](uint16_t a) { return cpu.ReadMemory(a); }, 0x8000);
            session.StepInstruction();
            const auto* event = session.History().Latest(0x8000);
            if (!event || !event->complete_capture || event->bytes.size() != decoded.length ||
                !std::equal(event->bytes.begin(), event->bytes.end(), code.begin())) {
                ++failures; std::cerr << "FAIL: opcode capture prefix=" << prefix.size() << " opcode=" << op << '\n';
            }
        }
    }
    std::cout << (failures ? "FAIL" : "PASS") << ": instruction evidence, mutation, history and address listing\n";
    return failures ? 1 : 0;
}
