// Copyright (c) 2026 Larry Dawson. Licensed under the MIT License (see LICENSE).
#include "transfer_analysis.h"
#include "debug_session.h"
#include <iostream>

using namespace z80::dbg;
using namespace z80::dbg::analysis;
namespace {
int failures = 0;
void check(bool ok, const char* message) {
    if (!ok) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
std::vector<DataAccess> word(DataAccessKind kind, uint16_t address, uint16_t value) {
    return {{kind, address, uint8_t(value)}, {kind, uint16_t(address + 1), uint8_t(value >> 8)}};
}
TransferSample step(std::string id, uint16_t pc, uint16_t next, std::vector<uint8_t> bytes,
                    uint16_t sp, uint16_t after, std::optional<std::string> previous,
                    std::vector<DataAccess> accesses = {}) {
    TransferSample s;
    s.id = std::move(id); s.event.start = pc; s.event.next_pc = next;
    s.event.bytes = std::move(bytes); s.event.read_count = s.event.bytes.size(); s.event.complete_capture = true;
    s.before.flags = 0;
    s.stack = StackEvidence{sp, after, true, std::move(previous), std::move(accesses)};
    return s;
}
TransferCapture nested() {
    return {"synthetic-nested", "Synthetic instruction-level accesses; no hardware or runtime identity claim", {
        step("call-outer", 0x8000, 0x8100, {0xCD, 0, 0x81}, 0xA000, 0x9FFE, {}, word(DataAccessKind::Write, 0x9FFE, 0x8003)),
        step("call-inner", 0x8100, 0x8200, {0xCD, 0, 0x82}, 0x9FFE, 0x9FFC, "call-outer", word(DataAccessKind::Write, 0x9FFC, 0x8103)),
        step("ret-inner", 0x8200, 0x8103, {0xC9}, 0x9FFC, 0x9FFE, "call-inner", word(DataAccessKind::Read, 0x9FFC, 0x8103)),
        step("ret-outer", 0x8103, 0x8003, {0xC9}, 0x9FFE, 0xA000, "ret-inner", word(DataAccessKind::Read, 0x9FFE, 0x8003))}};
}
TransferCapture through(TransferSample middle) {
    auto c = nested(); c.samples.resize(1);
    c.samples.push_back(std::move(middle));
    c.samples.push_back(step("return", c.samples[1].event.next_pc, 0x8003, {0xC9}, 0x9FFE, 0xA000,
                             c.samples[1].id, word(DataAccessKind::Read, 0x9FFE, 0x8003)));
    return c;
}
}
int main() {
    // Independent integration fixture: copy actual write-observer events and
    // actual data-read counter changes for CALL / JP (HL) / RET. These instructions
    // do not mix data reads and writes, so read values are unchanged at completion.
    // This test adapter makes no general runtime-capture or bus-order claim.
    {
        DebugCPU cpu; DebugSession session(cpu);
        cpu.LoadProgram({0xCD, 0x10, 0x80}, 0x8000);
        cpu.LoadProgram({0xE9}, 0x8010); cpu.LoadProgram({0xC9}, 0x8020);
        cpu.PC() = 0x8000; cpu.SP() = 0xA000; cpu.HL() = 0x8020;
        TransferCapture observed{"cpu-stack-fixture", "Instruction-level test capture; no run identity or bus timing", {}};
        std::optional<std::string> previous;
        for (const auto& id : {"cpu-call", "cpu-jump", "cpu-return"}) {
            std::vector<uint64_t> counts(65536);
            using Kind = z80::MetadataMemory::AccessKind;
            for (uint32_t a = 0; a < 65536; ++a)
                counts[a] = cpu.GetMemory().Metadata(uint16_t(a)).activity[size_t(Kind::DataRead)].count;
            StackEvidence stack{cpu.SP(), 0, true, previous, {}};
            TransferContext before{cpu.F(), cpu.B(), cpu.HL(), cpu.IX(), cpu.IY()};
            const auto observer = cpu.GetMemory().AddWriteObserver([&](uint16_t address, uint8_t, uint8_t value) {
                stack.accesses.push_back({DataAccessKind::Write, address, value});
            });
            session.StepInstruction();
            cpu.GetMemory().RemoveWriteObserver(observer);
            stack.after_sp = cpu.SP();
            for (uint32_t a = 0; a < 65536; ++a) {
                const auto delta = cpu.GetMemory().Metadata(uint16_t(a)).activity[size_t(Kind::DataRead)].count - counts[a];
                check(delta <= 1, "fixture data reads occur once per instruction/address");
                if (delta) stack.accesses.push_back({DataAccessKind::Read, uint16_t(a), cpu.ReadMemory(uint16_t(a))});
            }
            observed.samples.push_back({id, session.History().Events().back(), before, std::move(stack)});
            previous = id;
        }
        const auto actual = AnalyzeContinuations(observed);
        check(actual.back().status == "matched" && actual.back().call_sample == "cpu-call",
              "actual CPU writes and reads match across an indirect jump");
    }
    auto c = nested();
    auto f = AnalyzeContinuations(c);
    check(f[0].status == "created" && f[1].status == "created", "calls establish actual two-byte continuations");
    check(f[2].status == "matched" && f[2].call_sample == "call-inner" &&
          f[3].status == "matched" && f[3].call_sample == "call-outer", "nested returns match distinct creation events");
    check(f[3].explanation.find("call-outer") != std::string::npos, "explanation identifies supporting call");

    // Recursion/reused numeric continuation: distinguish by stack-slot lineage.
    c.samples[1].event.next_pc = 0x8100; c.samples[1].event.bytes = {0xCD, 0, 0x81};
    c.samples[2].event.start = 0x8100;
    f = AnalyzeContinuations(c);
    check(f[2].call_sample == "call-inner" && f[3].call_sample == "call-outer", "recursive destination does not collapse invocations");

    c = nested();
    c.samples[0].event.next_pc = 0x8000; c.samples[0].event.bytes = {0xCD, 0, 0x80};
    c.samples[1].event.start = 0x8000; c.samples[1].event.next_pc = 0x8000;
    c.samples[1].event.bytes = {0xCD, 0, 0x80};
    c.samples[1].stack->accesses = word(DataAccessKind::Write, 0x9FFC, 0x8003);
    c.samples[2].event.start = 0x8000; c.samples[2].event.next_pc = 0x8003;
    c.samples[2].stack->accesses = word(DataAccessKind::Read, 0x9FFC, 0x8003);
    c.samples[3].event.start = 0x8003;
    f = AnalyzeContinuations(c);
    check(f[2].call_sample == "call-inner" && f[3].call_sample == "call-outer",
          "identical numeric continuations remain distinct invocation identities");

    c = nested(); c.samples[2].stack->previous.reset();
    f = AnalyzeContinuations(c);
    check(f[2].status == "unresolved" && f[3].status == "unresolved", "capture gap discards all older lineage");
    c = nested(); c.samples[1].stack->complete_data_accesses = false;
    check(AnalyzeContinuations(c)[3].status == "unresolved", "missing writes prevent false outer match");
    c = nested(); c.samples[1].stack->previous = "missing";
    f = AnalyzeContinuations(c);
    check(f[2].status == "matched" && f[3].status == "unresolved", "new calls after a gap can establish local lineage");
    c = nested(); c.samples[1].event.start = 0x8110;
    check(AnalyzeContinuations(c)[3].status == "unresolved", "PC discontinuity invalidates older lineage");
    c = nested(); c.samples[0].stack->accesses.pop_back();
    check(AnalyzeContinuations(c)[3].status == "unresolved", "one written continuation byte is insufficient");
    c = nested(); c.samples[2].stack->accesses[1].value = 0x80;
    check(AnalyzeContinuations(c)[2].status == "unresolved", "contradictory read cannot match numeric PC alone");
    c = nested(); c.samples[2].stack->after_sp = 0x9FFF;
    check(AnalyzeContinuations(c)[2].status == "unresolved", "RET stack movement must match");

    c = through(step("rewrite", 0x8100, 0x8101, {0x77}, 0x9FFE, 0x9FFE, "call-outer",
                     {{DataAccessKind::Write, 0x9FFE, 3}}));
    check(AnalyzeContinuations(c).back().status == "unresolved", "same-value overwrite breaks original byte lineage");
    c.samples[1].stack->accesses[0].kind = DataAccessKind::RefusedWrite;
    check(AnalyzeContinuations(c).back().status == "matched", "refused write does not erase unchanged lineage");
    c = through(step("jump-interior", 0x8100, 0x8110, {0xC3, 0x10, 0x81}, 0x9FFE, 0x9FFE, "call-outer"));
    check(AnalyzeContinuations(c).back().status == "matched", "interior jump preserves an ordinary continuation");
    c.samples[1].event.bytes = {0xE9}; c.samples[1].event.read_count = 1; c.samples[1].before.hl = 0x8110;
    check(AnalyzeContinuations(c).back().status == "matched", "indirect jump need not close or split a routine");
    c.samples[1].before.hl = 0x8222;
    check(AnalyzeContinuations(c).back().status == "unresolved", "inconsistent indirect target invalidates continuity");

    c = through(step("untaken-return", 0x8100, 0x8101, {0xC0}, 0x9FFE, 0x9FFE, "call-outer"));
    c.samples[1].before.flags = 0x40;
    check(AnalyzeContinuations(c).back().status == "matched", "untaken conditional return does not consume a continuation");

    c = nested(); c.samples.resize(1);
    c.samples.push_back(step("push", 0x8100, 0x8101, {0xD5}, 0x9FFE, 0x9FFC, "call-outer", word(DataAccessKind::Write, 0x9FFC, 0x8003)));
    c.samples.push_back(step("dispatch-ret", 0x8101, 0x8003, {0xC9}, 0x9FFC, 0x9FFE, "push", word(DataAccessKind::Read, 0x9FFC, 0x8003)));
    check(AnalyzeContinuations(c).back().status == "unresolved", "PUSH/RET with equal numeric address is not an ordinary call return");
    c.samples[2] = step("pop", 0x8101, 0x8102, {0xD1}, 0x9FFC, 0x9FFE, "push", word(DataAccessKind::Read, 0x9FFC, 0x8003));
    c.samples.push_back(step("return", 0x8102, 0x8003, {0xC9}, 0x9FFE, 0xA000, "pop", word(DataAccessKind::Read, 0x9FFE, 0x8003)));
    check(AnalyzeContinuations(c).back().status == "matched", "ordinary balanced register saves preserve outer lineage");

    c = through(step("new-sp", 0x8100, 0x8103, {0x31, 0, 0x90}, 0x9FFE, 0x9000, "call-outer"));
    check(AnalyzeContinuations(c).back().status == "unresolved", "unmodeled SP replacement does not invent return matching");
    c = nested(); c.samples.erase(c.samples.begin(), c.samples.begin() + 2);
    check(AnalyzeContinuations(c).back().status == "unresolved", "capture starting at a return has no invented caller");

    c = {"wrap", "Synthetic wraparound stack", {
        step("call", 0x8000, 0x8100, {0xCD, 0, 0x81}, 1, 0xFFFF, {}, word(DataAccessKind::Write, 0xFFFF, 0x8003)),
        step("return", 0x8100, 0x8003, {0xC9}, 0xFFFF, 1, "call", word(DataAccessKind::Read, 0xFFFF, 0x8003))}};
    check(AnalyzeContinuations(c).back().status == "matched", "stack word wraps at 65535");
    c = {"rst", "Synthetic restart", {
        step("restart", 0x8000, 0x28, {0xEF}, 0xA000, 0x9FFE, {}, word(DataAccessKind::Write, 0x9FFE, 0x8001)),
        step("return", 0x28, 0x8001, {0xC9}, 0x9FFE, 0xA000, "restart", word(DataAccessKind::Read, 0x9FFE, 0x8001))}};
    check(AnalyzeContinuations(c).back().status == "matched", "RST continuation uses instruction length one");
    c.samples[0].stack->accesses[0].kind = DataAccessKind::RefusedWrite;
    check(AnalyzeContinuations(c).back().status == "unresolved", "refused call write cannot establish continuation");

    c = nested();
    const auto encoded = WriteTransferCapture(c);
    check(bool(encoded), "write version 2 capture");
    auto decoded = ReadTransferCapture(*encoded);
    check(bool(decoded) && *WriteTransferCapture(*decoded) == *encoded, "stack evidence round trip");
    check(*TransferReport(*decoded) == *TransferReport(c), "reanalysis after reopen is identical");
    const auto report = json::Parse(*TransferReport(c));
    const auto& occurrence = report.at("occurrences").array().back();
    check(occurrence.at("continuation").at("status").string() == "matched" &&
          occurrence.at("continuation").at("explanation").string().find("call-outer") != std::string::npos,
          "CLI report contains evidence-linked explanatory text");
    check(!report.at("destination_sets_closed").boolean(), "matching never closes possible usage");
    c.samples[1].stack->previous = c.samples[1].id;
    check(!WriteTransferCapture(c), "self-referential predecessor rejected");
    c = nested(); c.samples[0].stack->accesses.resize(kMaxDataAccesses + 1);
    check(!WriteTransferCapture(c), "data-access budget enforced");
    std::cout << (failures ? "FAIL" : "PASS") << ": continuation analysis\n";
    return failures ? 1 : 0;
}
