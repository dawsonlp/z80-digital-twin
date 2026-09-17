// Copyright (c) 2026 Larry Dawson. Licensed under the MIT License (see LICENSE).
#include "stack_analysis.h"
#include "debug_session.h"
#include <iostream>
using namespace z80::dbg;
using namespace z80::dbg::analysis;
namespace {
int failures = 0;
void check(bool ok, const char* message) { if (!ok) { ++failures; std::cerr << "FAIL: " << message << '\n'; } }
std::vector<DataAccess> word(DataAccessKind kind, uint16_t address, uint16_t value) {
    return {{kind, address, uint8_t(value)}, {kind, uint16_t(address + 1), uint8_t(value >> 8)}};
}
struct Fixture {
    TransferCapture capture{"synthetic-constructed", "Synthetic effects, not a ROM capture", {}};
    uint16_t pc = 0x8000, sp = 0xA000;
    void add(std::vector<uint8_t> bytes, std::optional<uint16_t> next = {},
             std::optional<uint16_t> after = {}, std::vector<DataAccess> accesses = {}) {
        TransferSample s;
        s.id = "s" + std::to_string(capture.samples.size());
        s.event.start = pc; s.event.next_pc = next.value_or(uint16_t(pc + bytes.size()));
        s.event.read_count = bytes.size(); s.event.bytes = std::move(bytes); s.event.complete_capture = true;
        s.stack = StackEvidence{sp, after.value_or(sp), true,
            capture.samples.empty() ? std::nullopt : std::optional<std::string>(capture.samples.back().id), std::move(accesses)};
        pc = s.event.next_pc; sp = s.stack->after_sp; capture.samples.push_back(std::move(s));
    }
    void call() { add({0xCD, 0, 0x81}, 0x8100, uint16_t(sp - 2), word(DataAccessKind::Write, uint16_t(sp - 2), uint16_t(pc + 3))); }
    void pop(std::vector<uint8_t> bytes = {0xE1}, uint16_t target = 0x8003) {
        add(std::move(bytes), {}, uint16_t(sp + 2), word(DataAccessKind::Read, sp, target));
    }
    void push(uint16_t target) { add({0xE5}, {}, uint16_t(sp - 2), word(DataAccessKind::Write, uint16_t(sp - 2), target)); }
    void ret(uint16_t target) { add({0xC9}, target, uint16_t(sp + 2), word(DataAccessKind::Read, sp, target)); }
    std::vector<StackFinding> analyze(size_t budget = kMaxValueNodes) const {
        const auto c = AnalyzeContinuations(capture); const auto v = AnalyzeAddressValues(capture, budget);
        return AnalyzeStackReconstruction(capture, c, v, AnalyzeConstructedTransfers(capture, c, v));
    }
};
}
int main() {
    // Independent integration fixture: copy actual write-observer events and
    // actual data-read counter changes for CALL / CALL / POP HL / RET. These instructions
    // do not mix data reads and writes, so read values are unchanged at completion.
    // This test adapter makes no general runtime-capture or bus-order claim.
    {
        DebugCPU cpu; DebugSession session(cpu);
        cpu.LoadProgram({0xCD, 0x10, 0x80}, 0x8000);
        cpu.LoadProgram({0xCD, 0x20, 0x80}, 0x8010);
        cpu.LoadProgram({0xE1, 0xC9}, 0x8020);
        cpu.PC() = 0x8000; cpu.SP() = 0xA000; cpu.HL() = 0x8020;
        TransferCapture observed{"cpu-stack-fixture", "Instruction-level test capture; no run identity or bus timing", {}};
        std::optional<std::string> previous;
        for (const auto& id : {"cpu-outer", "cpu-inner", "cpu-pop", "cpu-return"}) {
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
        const auto values = AnalyzeAddressValues(observed);
        const auto calls = AnalyzeContinuations(observed);
        const auto result = AnalyzeStackReconstruction(observed, calls, values, AnalyzeConstructedTransfers(observed, calls, values));
        check(result.back().pattern == "caller_skipping_exit" && result.back().call_sample == "cpu-outer" &&
              result.back().skipped_calls == std::vector<std::string>{"cpu-inner"},
              "actual CPU effects establish an outer return skipping the inner caller");
    }

    Fixture f; f.call(); f.call(); f.pop({0xE1}, 0x8103); f.ret(0x8003);
    auto a = f.analyze();
    check(a[2].pattern == "continuation_removed_from_stack", "POP leaves register use open");
    check(a.back().pattern == "caller_skipping_exit" && a.back().call_sample == "s0" &&
          a.back().skipped_calls == std::vector<std::string>{"s1"} && a.back().return_role_established,
          "inner POP then outer RET identifies actual caller-skipping exit");
    check(a.back().supporting_samples == std::vector<std::string>{"s2"}, "skip includes the removal proof");
    check(*TransferReport(f.capture) == *TransferReport(*ReadTransferCapture(*WriteTransferCapture(f.capture))), "stack findings survive reopen");
    f.capture.samples[2].stack->previous.reset();
    check(f.analyze().back().pattern != "caller_skipping_exit", "capture gap loses outer invocation context");

    f = {}; f.call(); f.call(); f.add({0x33}, {}, 0x9FFD); f.add({0x33}, {}, 0x9FFE); f.ret(0x8003);
    check(f.analyze().back().pattern == "caller_skipping_exit", "two INC SP effects prove byte-wise removal");
    f = {}; f.sp = 1; f.call(); f.call(); f.pop({0xE1}, 0x8103); f.ret(0x8003);
    check(f.analyze().back().pattern == "caller_skipping_exit", "caller-skipping handles stack wrap");
    f = {}; f.call(); f.call(); f.call(); f.add({0x31, 0xFE, 0x9F}, {}, 0x9FFE); f.ret(0x8003);
    check(f.analyze().back().skipped_calls.size() == 2, "SP assignment between live slots proves multiple bypasses");

    f = {}; f.call(); f.pop(); f.push(0x8003); f.ret(0x8003);
    check(f.analyze().back().pattern == "reconstructed_continuation_return", "PUSH reconstructs exact captured call continuation");
    f.add({0xE9}, 0x8003);
    check(!f.analyze().back().return_role_established, "reconstructed return cannot consume original call twice");
    f = {}; f.call(); f.pop(); f.add({0x31, 0, 0x90}, {}, 0x9000); f.push(0x8003); f.ret(0x8003);
    check(f.analyze().back().pattern == "continuation_on_rebuilt_stack" && !f.analyze().back().return_role_established,
          "relocated continuation does not claim original caller stack restored");

    f = {}; f.add({0x21, 0, 0}); f.add({0x39}); f.add({0x31, 0, 0x90}, {}, 0x9000); f.add({0xF9}, {}, 0xA000);
    check(f.analyze().back().pattern == "restored_stack_pointer", "saved SP copied with ADD HL,SP restores by provenance");
    f.capture.samples.back().event.bytes = {0x31, 0, 0xA0};
    f.capture.samples.back().event.read_count = 3; f.capture.samples.back().event.next_pc += 2;
    check(f.analyze().back().pattern == "stack_pointer_changed", "equal immediate SP is not saved-pointer provenance");
    f = {}; f.add({0x31, 0, 0xB0}, {}, 0xB000); f.add({0x21, 0, 0}); f.add({0x39});
    f.add({0x31, 0, 0x90}, {}, 0x9000); f.add({0xF9}, {}, 0xB000);
    check(f.analyze().back().pattern == "restored_stack_pointer", "saved SP provenance also begins after an explicit initial assignment");
    f = {}; f.add({0x21, 0, 0}); f.add({0x39});
    f.add({0x22, 0, 0x91}, {}, {}, word(DataAccessKind::Write, 0x9100, 0xA000));
    f.add({0x31, 0, 0x90}, {}, 0x9000);
    f.add({0xED, 0x7B, 0, 0x91}, {}, 0xA000, word(DataAccessKind::Read, 0x9100, 0xA000));
    check(f.analyze().back().pattern == "restored_stack_pointer", "spilled pointer restores via LD SP,(nn)");
    f.capture.samples.back().stack->accesses[0].value = 1;
    check(f.analyze().back().status == "unresolved", "contradictory reload prevents saved-SP claim");

    f = {}; f.call(); f.add({0x21, 0, 0x90});
    f.add({0x22, 0xFE, 0x9F}, {}, {}, word(DataAccessKind::Write, 0x9FFE, 0x9000)); f.ret(0x9000);
    check(f.analyze().back().pattern == "substituted_continuation_transfer" && !f.analyze().back().return_role_established,
          "replacement stack target names original slot without asserting original return");
    f = {}; f.call(); f.add({0x21, 3, 0x80});
    f.add({0x22, 0xFE, 0x9F}, {}, {}, word(DataAccessKind::Write, 0x9FFE, 0x8003)); f.ret(0x8003);
    check(f.analyze().back().pattern == "substituted_continuation_transfer", "same-number unrelated replacement is not original identity");

    f = {}; f.add({0x21, 0, 0x82}); f.push(0x8200); f.add({0x21, 0, 0x90}); f.push(0x9000); f.ret(0x9000);
    auto prefix = f.analyze();
    check(prefix.back().pattern == "prepared_continuation_candidate" && !prefix.back().return_role_established,
          "prepared literal is only a candidate before consumption");
    f.ret(0x8200);
    a = f.analyze();
    check(a.back().pattern == "constructed_continuation_consumed" && a.back().return_role_established,
          "actual later RET establishes prepared continuation consumption");
    check(json::Write(StackFindingJson(a[4])) == json::Write(StackFindingJson(prefix[4])), "later consumption does not rewrite earlier candidate");
    check(f.analyze(0).back().status == "unresolved", "value budget bound prevents invented stack reconstruction");
    // Repeated dispatches may leave the same prepared word in place. They do
    // not create additional consumable identities for it.
    f = {}; f.add({0x21, 0, 0x82}); f.push(0x8200); f.add({0x21, 0, 0x90}); f.push(0x9000); f.ret(0x9000);
    f.push(0x9000); f.ret(0x9000); f.ret(0x8200);
    check(f.analyze().back().pattern == "constructed_continuation_consumed", "one literal survives multiple dispatches");
    f.add({0x31, 0xFE, 0x9F}, {}, 0x9FFE); f.ret(0x8200);
    check(!f.analyze().back().return_role_established, "old prepared bytes cannot be consumed twice without new preparation");

    f = {}; f.add({0x21, 0, 0x82}); f.push(0x8200); f.add({0x21, 0, 0x90}); f.push(0x9000); f.ret(0x9000);
    f.add({0x21, 0xFE, 0x9F}); f.add({0x3E, 0});
    f.add({0x77}, {}, {}, {{DataAccessKind::Write, 0x9FFE, 0}}); f.ret(0x8200);
    check(!f.analyze().back().return_role_established, "equal-value overwrite invalidates prepared literal identity");
    f.capture.samples[7].stack->accesses[0].kind = DataAccessKind::RefusedWrite;
    check(f.analyze().back().return_role_established, "refused overwrite preserves prepared literal identity");

    std::cout << (failures ? "FAIL" : "PASS") << ": stack reconstruction\n";
    return failures ? 1 : 0;
}
