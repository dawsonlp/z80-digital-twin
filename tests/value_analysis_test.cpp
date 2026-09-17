// Copyright (c) 2026 Larry Dawson. Licensed under the MIT License (see LICENSE).
#include "value_analysis.h"
#include "constructed_analysis.h"
#include "debug_session.h"
#include <iostream>
#include <set>

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
struct Fixture {
    TransferCapture capture{"synthetic-value-flow", "Synthetic instruction effects, not a ROM trace", {}};
    uint16_t pc = 0x8000, sp = 0xA000;
    void add(std::vector<uint8_t> bytes, std::optional<uint16_t> next = {},
             std::optional<uint16_t> after_sp = {}, std::vector<DataAccess> accesses = {}) {
        TransferSample s;
        s.id = "s" + std::to_string(capture.samples.size());
        s.event.start = pc; s.event.next_pc = next.value_or(uint16_t(pc + bytes.size()));
        s.event.read_count = bytes.size(); s.event.bytes = std::move(bytes); s.event.complete_capture = true;
        s.stack = StackEvidence{sp, after_sp.value_or(sp), true,
            capture.samples.empty() ? std::nullopt : std::optional<std::string>(capture.samples.back().id), std::move(accesses)};
        pc = s.event.next_pc; sp = s.stack->after_sp;
        capture.samples.push_back(std::move(s));
    }
};
std::vector<const ValueNode*> ancestors(const ValueAnalysis& result) {
    std::vector<const ValueNode*> nodes;
    if (!result.findings.back().root) return nodes;
    std::vector<ValueNodeId> pending{*result.findings.back().root};
    std::set<uint32_t> visited;
    while (!pending.empty()) {
        const auto id = pending.back(); pending.pop_back();
        if (!visited.insert(id.value).second) continue;
        const auto& n = result.nodes.at(id.value - 1); nodes.push_back(&n);
        pending.insert(pending.end(), n.inputs.begin(), n.inputs.end());
    }
    return nodes;
}
bool has(const ValueAnalysis& result, std::string_view operation, std::string_view sample = {}) {
    for (const auto* n : ancestors(result))
        if (n->operation == operation && (sample.empty() || n->sample_id == sample)) return true;
    return false;
}
}
int main() {
    // Independent integration fixture: copy actual write-observer events and
    // actual data-read counter changes for CALL / POP DE / EX DE,HL / JP (HL). These instructions
    // do not mix data reads and writes, so read values are unchanged at completion.
    // This test adapter makes no general runtime-capture or bus-order claim.
    {
        DebugCPU cpu; DebugSession session(cpu);
        cpu.LoadProgram({0xCD, 0x10, 0x80}, 0x8000);
        cpu.LoadProgram({0xD1, 0xEB, 0xE9}, 0x8010);
        cpu.PC() = 0x8000; cpu.SP() = 0xA000; cpu.HL() = 0x8020;
        TransferCapture observed{"cpu-stack-fixture", "Instruction-level test capture; no run identity or bus timing", {}};
        std::optional<std::string> previous;
        for (const auto& id : {"cpu-call", "cpu-pop", "cpu-exchange", "cpu-jump"}) {
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
        const auto actual = AnalyzeAddressValues(observed);
        const auto constructed = AnalyzeConstructedTransfers(observed, AnalyzeContinuations(observed), actual);
        check(constructed.back().return_role_established && constructed.back().call_sample == "cpu-call",
              "actual CPU execution supports a register-mediated return finding");
        check(actual.findings.back().status == ValueStatus::Traced &&
              has(actual, "call_continuation", "cpu-call") && has(actual, "exchange", "cpu-exchange"),
              "actual CPU execution traces a popped continuation through EX to JP (HL)");
    }

    Fixture f;
    f.add({0x21, 0x23, 0x81}); f.add({0xE9}, 0x8123);
    auto a = AnalyzeAddressValues(f.capture);
    check(a.findings.back().status == ValueStatus::Traced && has(a, "immediate", "s0"), "immediate HL target traced");
    const auto saved_report = *TransferReport(f.capture);
    check(*TransferReport(*ReadTransferCapture(*WriteTransferCapture(f.capture))) == saved_report,
          "value graph survives save/reopen deterministically");

    f = {}; f.add({0x26, 0x81}); f.add({0x2E, 0x23}); f.add({0x54}); f.add({0x5D}); f.add({0xEB}); f.add({0xE9}, 0x8123);
    a = AnalyzeAddressValues(f.capture);
    check(a.findings.back().status == ValueStatus::Traced && has(a, "register_copy") && has(a, "exchange", "s4"),
          "byte copies and EX retain their supporting observations");

    f = {}; f.add({0x21, 0, 0x90});
    f.add({0x5E}, {}, {}, {{DataAccessKind::Read, 0x9000, 0x23}}); f.add({0x23});
    f.add({0x56}, {}, {}, {{DataAccessKind::Read, 0x9001, 0x81}}); f.add({0xEB}); f.add({0xE9}, 0x8123);
    a = AnalyzeAddressValues(f.capture);
    check(a.findings.back().status == ValueStatus::Partial && has(a, "memory_read_before_trace") && has(a, "inc16"),
          "ROM-style pointer assembly identifies memory boundary and address arithmetic");
    bool low = false, high = false;
    for (const auto* n : ancestors(a)) { low |= n->memory_address == 0x9000; high |= n->memory_address == 0x9001; }
    check(low && high, "both pointer byte addresses are retained");

    f = {}; f.add({0xDD, 0x21, 0xD1, 3}); f.add({0x01, 3, 0}); f.add({0xDD, 0x09}); f.add({0xDD, 0xE9}, 0x03D4);
    a = AnalyzeAddressValues(f.capture);
    check(a.findings.back().status == ValueStatus::Traced && has(a, "add16"), "indexed timing target computed from base plus offset");
    f.capture.samples.back().before.ix = 0x03D5;
    check(AnalyzeAddressValues(f.capture).findings.back().status == ValueStatus::Unresolved, "contradictory entry register breaks lineage");

    f = {}; f.add({0x21, 0xFF, 0xFF}); f.add({0x11, 2, 0}); f.add({0x19}); f.add({0xE9}, 1);
    a = AnalyzeAddressValues(f.capture);
    check(a.findings.back().status == ValueStatus::Traced && a.nodes[a.findings.back().root->value - 1].value == 1,
          "16-bit address addition wraps explicitly");
    f = {}; f.add({0x21, 0, 0x81}); f.add({0x09}); f.add({0xE9}, 0x8101);
    check(AnalyzeAddressValues(f.capture).findings.back().status == ValueStatus::Unresolved, "unknown arithmetic operand is never guessed");

    f = {}; f.add({0x21, 0x23, 0x81}); f.add({0xD9}); f.add({0x21, 0, 0x99}); f.add({0xD9}); f.add({0xE9}, 0x8123);
    a = AnalyzeAddressValues(f.capture);
    check(a.findings.back().status == ValueStatus::Traced && has(a, "exchange", "s1") && has(a, "exchange", "s3"),
          "EXX round trip preserves alternate-bank lineage");

    f = {}; f.add({0xCD, 0x10, 0x80}, 0x8010, 0x9FFE, word(DataAccessKind::Write, 0x9FFE, 0x8003));
    f.add({0xD1}, {}, 0xA000, word(DataAccessKind::Read, 0x9FFE, 0x8003)); f.add({0xEB}); f.add({0xE9}, 0x8003);
    a = AnalyzeAddressValues(f.capture);
    check(a.findings.back().status == ValueStatus::Traced && has(a, "call_continuation", "s0"),
          "CALL -> POP DE -> EX DE,HL -> JP (HL) traces to the saved continuation");
    const auto popped = json::Parse(*TransferReport(f.capture));
    check(popped.at("occurrences").array().back().at("target_basis").string() == "traced_value_origin",
          "report exposes refined value origin");
    f = {}; f.add({0x21, 3, 0x80}); f.add({0xE9}, 0x8003);
    check(!has(AnalyzeAddressValues(f.capture), "call_continuation"), "equal numeric immediate is not a saved continuation");

    f = {}; f.add({0x21, 0x23, 0x81}); f.add({0xE5}, {}, 0x9FFE, word(DataAccessKind::Write, 0x9FFE, 0x8123));
    f.add({0xFD, 0xE1}, {}, 0xA000, word(DataAccessKind::Read, 0x9FFE, 0x8123)); f.add({0xFD, 0xE9}, 0x8123);
    check(AnalyzeAddressValues(f.capture).findings.back().status == ValueStatus::Traced, "PUSH/POP to IY preserves value origin");

    f = {}; f.add({0x21, 0x23, 0x81}); f.add({0x22, 0, 0x90}, {}, {}, word(DataAccessKind::Write, 0x9000, 0x8123));
    f.add({0xED, 0x5B, 0, 0x90}, {}, {}, word(DataAccessKind::Read, 0x9000, 0x8123)); f.add({0xEB}); f.add({0xE9}, 0x8123);
    a = AnalyzeAddressValues(f.capture);
    check(a.findings.back().status == ValueStatus::Traced && has(a, "memory_write", "s1") && has(a, "memory_read", "s2"),
          "absolute word spill/reload retains observed write version");
    f.capture.samples[2].stack->accesses[0].value = 0x24;
    check(AnalyzeAddressValues(f.capture).findings.back().status == ValueStatus::Unresolved, "contradictory memory read discards old provenance");

    f = {}; f.add({0x21, 0x23, 0x81}); f.add({0x22, 0, 0x90}, {}, {}, word(DataAccessKind::Write, 0x9000, 0x8123));
    f.add({0x21, 0, 0x90}); f.add({0x3E, 0x23}); f.add({0x77}, {}, {}, {{DataAccessKind::Write, 0x9000, 0x23}});
    f.add({0x2A, 0, 0x90}, {}, {}, word(DataAccessKind::Read, 0x9000, 0x8123)); f.add({0xE9}, 0x8123);
    a = AnalyzeAddressValues(f.capture);
    check(a.findings.back().status == ValueStatus::Traced && has(a, "memory_write", "s4"), "same-value writes create distinct value versions");
    f.capture.samples[4].stack->accesses[0].kind = DataAccessKind::RefusedWrite;
    check(!has(AnalyzeAddressValues(f.capture), "memory_write", "s4"), "refused writes preserve the previous value version");

    f = {}; f.add({0x21, 0x23, 0x81}); f.add({0xE3}, {}, {}, {
        {DataAccessKind::Read, 0xA000, 0}, {DataAccessKind::Read, 0xA001, 0x82},
        {DataAccessKind::Write, 0xA000, 0x23}, {DataAccessKind::Write, 0xA001, 0x81}});
    f.add({0xE9}, 0x8200);
    check(AnalyzeAddressValues(f.capture).findings.back().status == ValueStatus::Partial, "EX (SP),HL preserves read/write origins with pretrace memory boundary");

    f = {}; f.add({0x21, 0x23, 0x81}); f.add({0xA7}); f.add({0xE9}, 0x8123);
    check(AnalyzeAddressValues(f.capture).findings.back().status == ValueStatus::Unresolved, "unsupported opcode does not silently preserve register lineage");
    f.capture.samples.back().before.hl = 0x8123;
    check(AnalyzeAddressValues(f.capture).findings.back().status == ValueStatus::Partial, "later snapshot is a new boundary, not restoration of lost lineage");
    f = {}; f.add({0x21, 0x23, 0x81}); f.add({0xE9}, 0x8123); f.capture.samples.back().stack->previous.reset();
    check(AnalyzeAddressValues(f.capture).findings.back().status == ValueStatus::Unresolved, "capture gap stops value provenance");
    f.capture.samples.back().stack->previous = "s0"; f.capture.samples[0].stack->complete_data_accesses = false;
    check(AnalyzeAddressValues(f.capture).findings.back().status == ValueStatus::Unresolved, "incomplete accesses stop provenance");

    f = {}; f.add({0x21, 0x23, 0x81}); f.add({0xE9}, 0x8123);
    a = AnalyzeAddressValues(f.capture, 2);
    check(a.exhausted && a.nodes.size() <= 2 && a.findings.back().status == ValueStatus::Unresolved, "node budget explicitly stops analysis");
    a = AnalyzeAddressValues(f.capture, 0);
    check(a.exhausted && a.nodes.empty(), "zero node budget produces no invented graph");

    f = {}; f.add({0xCD, 0x10, 0x80}, 0x8010, 0x9FFE, word(DataAccessKind::Write, 0x9FFE, 0x8003));
    f.add({0xC9}, 0x8003, 0xA000, word(DataAccessKind::Read, 0x9FFE, 0x8003));
    const auto report = json::Parse(*TransferReport(f.capture)); const auto& occurrence = report.at("occurrences").array().back();
    check(occurrence.at("target_basis").string() == "matched_call_continuation" &&
          occurrence.at("completeness").at("target_provenance").string() == "matched_call_continuation" &&
          occurrence.at("instruction_effect").at("target_basis").string() == "stack_origin_unresolved",
          "later proven origin refines top-level result while preserving earlier tactic scope");
    std::cout << (failures ? "FAIL" : "PASS") << ": value analysis\n";
    return failures ? 1 : 0;
}
