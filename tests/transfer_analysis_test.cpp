// Copyright (c) 2026 Larry Dawson. Licensed under the MIT License (see LICENSE).
#include "transfer_analysis.h"
#include "debug_session.h"
#include <iostream>
#include <limits>

using namespace z80::dbg;
using namespace z80::dbg::analysis;
namespace {
int failures = 0;
void check(bool ok, const char* message) {
    if (!ok) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
TransferSample sample(std::string id, uint16_t pc, uint16_t next, std::vector<uint8_t> bytes,
                      TransferContext before = {}) {
    InstructionObservation event;
    event.sequence = 1; event.start = pc; event.next_pc = next;
    event.complete_capture = true; event.read_count = bytes.size(); event.bytes = std::move(bytes);
    return {std::move(id), std::move(event), before};
}
TransferSample execute(DebugSession& session, std::string id) {
    auto& cpu = session.Cpu();
    TransferContext before{cpu.F(), cpu.B(), cpu.HL(), cpu.IX(), cpu.IY()};
    session.StepInstruction();
    return {std::move(id), session.History().Events().back(), before};
}
}
int main() {
    // Real CPU behavior: target equals fall-through for BOTH outcomes. PC alone
    // cannot distinguish a taken CALL; saved flags can, even after persistence.
    DebugCPU cpu; DebugSession session(cpu);
    cpu.LoadProgram({0xC4, 0x03, 0x80, 0xC9}, 0x8000); // CALL NZ,$8003; RET
    cpu.PC() = 0x8000; cpu.SP() = 0xA000; cpu.F() = 0;
    const auto taken = execute(session, "taken-call");
    check(cpu.SP() == 0x9FFE, "fixture actually took CALL");
    const auto returned = execute(session, "return");
    check(cpu.PC() == 0x8003 && cpu.SP() == 0xA000, "fixture actually consumed continuation");
    cpu.PC() = 0x8000; cpu.F() = 0x40;
    const auto not_taken = execute(session, "untaken-call");
    check(cpu.SP() == 0xA000, "fixture did not take CALL");
    check(ClassifyTransfer(taken).taken == true && ClassifyTransfer(not_taken).taken == false,
          "flags distinguish identical CALL successors");
    auto missing = taken; missing.id = "no-context"; missing.before = {};
    check(!ClassifyTransfer(missing).taken, "missing flags remain unresolved despite successor");
    check(ClassifyTransfer(returned).mechanism == TransferMechanism::Return &&
          !ClassifyTransfer(returned).unresolved.empty(), "RET does not claim a matched logical return");

    // Every Z80 condition, both outcomes, on CALL, JP and RET.
    constexpr std::array<uint8_t, 8> true_flags = {0, 0x40, 0, 1, 0, 4, 0, 0x80};
    constexpr std::array<uint8_t, 8> false_flags = {0x40, 0, 1, 0, 4, 0, 0x80, 0};
    for (unsigned c = 0; c < 8; ++c) for (bool yes : {false, true}) {
        TransferContext context; context.flags = yes ? true_flags[c] : false_flags[c];
        auto call = sample("call", 0x8000, 0x8003, {uint8_t(0xC4 + 8*c), 3, 0x80}, context);
        check(ClassifyTransfer(call).taken == yes, "all CALL conditions");
        call.event.bytes[0] = uint8_t(0xC2 + 8*c);
        check(ClassifyTransfer(call).taken == yes, "all JP conditions");
        auto ret = sample("ret", 0x8000, 0x8001, {uint8_t(0xC0 + 8*c)}, context);
        check(ClassifyTransfer(ret).taken == yes, "all RET conditions");
    }
    for (unsigned c = 0; c < 4; ++c) {
        TransferContext context; context.flags = true_flags[c];
        auto relative = sample("jr", 0xFFFE, 0, {uint8_t(0x20 + 8*c), 0}, context);
        check(ClassifyTransfer(relative).taken == true && ClassifyTransfer(relative).encoded_target == 0,
              "relative conditions and address wrap");
    }
    TransferContext counter; counter.b = 1;
    auto djnz = sample("djnz", 0x8000, 0x8002, {0x10, 0}, counter);
    check(ClassifyTransfer(djnz).taken == false, "DJNZ decrements B before condition");
    djnz.before.b = 0;
    check(ClassifyTransfer(djnz).taken == true, "DJNZ zero wraps to 255");

    // Same destination can be reached by several mechanisms, and an indirect
    // site can acquire another destination later. Nothing closes the set.
    auto jump1 = sample("jump-a", 0x9000, 0x8010, {0xE9}, {});
    jump1.before.hl = 0x8010;
    auto jump2 = jump1; jump2.id = "jump-b"; jump2.event.next_pc = 0x8012; jump2.before.hl = 0x8012;
    auto prefixed = sample("index", 0x9000, 0x8123, {0xDD, 0xFD, 0xE9});
    prefixed.before.ix = 0x9999; prefixed.before.iy = 0x8123;
    check(ClassifyTransfer(prefixed).mechanism == TransferMechanism::IndirectJump &&
          ClassifyTransfer(prefixed).unresolved.size() == 1, "last index prefix determines target register");
    auto mismatch = prefixed; mismatch.before.iy = 0x8124;
    check(ClassifyTransfer(mismatch).unresolved.front().find("disagrees") != std::string::npos,
          "contradictory register context remains visible");
    auto repeat = sample("repeat", 0x8000, 0x8000, {0xED, 0xB0});
    check(ClassifyTransfer(repeat).mechanism == TransferMechanism::Repeat && ClassifyTransfer(repeat).taken == true,
          "LDIR repetition is not misclassified as sequential");
    repeat.event.next_pc = 0x8002;
    check(ClassifyTransfer(repeat).taken == false, "LDIR completion");
    auto reti = sample("reti", 0x8000, 0x9876, {0xED, 0x4D});
    check(ClassifyTransfer(reti).mechanism == TransferMechanism::InterruptReturn, "RETI has distinct category");
    auto rst = sample("rst", 0x8000, 0x28, {0xEF});
    check(ClassifyTransfer(rst).mechanism == TransferMechanism::Restart && ClassifyTransfer(rst).encoded_target == 0x28,
          "RST is distinct from ordinary CALL");
    auto partial = sample("partial", 0x8000, 0x8001, {0xDD}); partial.event.complete_capture = false;
    check(ClassifyTransfer(partial).mechanism == TransferMechanism::Unknown, "partial capture stays unknown");
    partial.event.complete_capture = true;
    check(ClassifyTransfer(partial).mechanism == TransferMechanism::Unknown, "unterminated prefixes stay unknown");
    auto extra = sample("extra", 0x8000, 0x8001, {0, 0});
    check(ClassifyTransfer(extra).mechanism == TransferMechanism::Unknown, "two instructions cannot masquerade as one");
    TransferSample machine; machine.id = "transition";
    machine.event.kind = ObservationKind::MachineTransition; machine.event.start = 0x8888; machine.event.next_pc = 0x38;
    check(ClassifyTransfer(machine).mechanism == TransferMechanism::MachineTransition &&
          !ClassifyTransfer(machine).taken, "machine event is not invented CALL or confirmed interrupt");

    TransferCapture capture{"synthetic-case-1", "Instruction evidence only; stack and hardware fidelity not established",
        {taken, returned, not_taken, missing, jump1, jump2, prefixed, rst, machine}};
    capture.samples[0].event.cycles = std::numeric_limits<uint64_t>::max();
    const auto serialized = WriteTransferCapture(capture);
    check(bool(serialized), "serialize capture");
    const auto reopened = ReadTransferCapture(*serialized);
    check(bool(reopened) && reopened->samples[0].event.cycles == std::numeric_limits<uint64_t>::max(),
          "persistence retains unsigned 64-bit values exactly");
    check(*WriteTransferCapture(*reopened) == *serialized, "capture reserialization is deterministic");
    auto report = TransferReport(capture);
    check(bool(report) && *report == *TransferReport(*reopened), "reopened evidence gives identical analysis");
    const auto json = z80::dbg::json::Parse(*report);
    check(!json.at("destination_sets_closed").boolean() && json.at("occurrences").array().size() == capture.samples.size(),
          "every occurrence retained; destination sets stay open");
    // Evidence is a value: neither transient eviction nor subsequent memory edits
    // may change its serialized content or reanalysis.
    cpu.LoadProgram({0x18, 0xFE}, 0x9000); cpu.PC() = 0x9000;
    session.RunSlice(InstructionHistory::kCapacity + 1);
    cpu.WriteMemory(0x8000, 0);
    check(*TransferReport(capture) == *report, "copied evidence survives history eviction and source-byte mutation");
    capture.samples.push_back(sample("later-call", 0x8120, 0x8012, {0xCD, 0x12, 0x80}));
    const auto later = z80::dbg::json::Parse(*TransferReport(capture));
    for (size_t i = 0; i < json.at("occurrences").array().size(); ++i)
        check(json.at("occurrences").array()[i] == later.at("occurrences").array()[i], "later evidence does not rewrite earlier occurrence");
    capture.samples.push_back(capture.samples.front());
    check(!WriteTransferCapture(capture) && !TransferReport(capture), "duplicate observation identities rejected");
    auto malformed = z80::dbg::json::Parse(*serialized);
    std::get<z80::dbg::json::Value::Object>(malformed.data)["version"] = 99;
    check(!ReadTransferCapture(z80::dbg::json::Write(malformed)), "unknown schema rejected");
    check(!ReadTransferCapture("{"), "truncated capture rejected");
    std::cout << (failures ? "FAIL" : "PASS") << ": transfer analysis\n";
    return failures ? 1 : 0;
}
