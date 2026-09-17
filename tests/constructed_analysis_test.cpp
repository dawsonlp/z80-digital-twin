// Copyright (c) 2026 Larry Dawson. Licensed under the MIT License (see LICENSE).
#include "constructed_analysis.h"
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
    std::vector<ConstructedFinding> analyze(size_t budget = kMaxValueNodes) const {
        return AnalyzeConstructedTransfers(capture, AnalyzeContinuations(capture), AnalyzeAddressValues(capture, budget));
    }
};
}
int main() {
    Fixture f; f.call(); f.pop({0xD1}); f.add({0xEB}); f.add({0xE9}, 0x8003);
    auto found = f.analyze();
    check(found.back().pattern == "popped_continuation_jump" && found.back().return_role_established &&
          found.back().call_sample == "s0" && found.back().pop_sample == "s1", "popped DE exchanged into HL is a supported return");
    auto report = *TransferReport(f.capture);
    check(report == *TransferReport(*ReadTransferCapture(*WriteTransferCapture(f.capture))), "constructed finding survives deterministic reopen");
    f.add({0xE9}, 0x8003);
    check(f.analyze().back().status == "unresolved", "register copy cannot consume the same call twice");

    f = {}; f.call(); f.pop(); f.add({0x23}); f.add({0x2B}); f.add({0xE9}, 0x8003);
    check(f.analyze().back().status == "unresolved", "arithmetic returning to the same number does not preserve exact continuation identity");
    f = {}; f.call(); f.pop(); f.add({0x21, 3, 0x80}); f.add({0xE9}, 0x8003);
    check(f.analyze().back().status == "unresolved", "equal immediate is not a saved continuation");
    f = {}; f.call(); f.pop({0xDD, 0xE1}); f.add({0xDD, 0xE9}, 0x8003);
    check(f.analyze().back().return_role_established, "index register POP and jump preserve byte identity");
    f.capture.samples.back().stack->previous.reset();
    check(f.analyze().back().status == "unresolved", "gap prevents a register return claim");
    f = {}; f.sp = 1; f.call(); f.pop(); f.add({0xE9}, 0x8003);
    check(f.analyze().back().return_role_established, "continuation stack wraps correctly");

    f = {}; f.call(); f.pop(); f.push(0x8003); f.ret(0x8003); f.add({0xE9}, 0x8003);
    found = f.analyze();
    check(found[3].pattern == "pushed_target_ret" && !found[3].return_role_established &&
          found.back().status == "unresolved", "PUSH/RET consumes old call identity without inventing an ordinary return or reuse");

    f = {}; f.call(); f.pop(); f.push(0x8003);
    f.add({0xED, 0x45}, 0x8003, 0xA000, word(DataAccessKind::Read, 0x9FFE, 0x8003));
    f.add({0xE9}, 0x8003);
    check(f.analyze().back().status == "unresolved", "interrupt-return instruction retires uncertain invocation lifetimes");

    f = {}; f.add({0x21, 0, 0x90}); f.push(0x9000); f.ret(0x9000);
    found = f.analyze();
    check(found.back().pattern == "pushed_target_ret" && found.back().push_sample == "s1" && !found.back().return_role_established,
          "RET dispatch is linked to actual PUSH, not a fabricated CALL");
    f = {}; f.push(0x9000); f.ret(0x9000);
    check(f.analyze().back().pattern == "pushed_target_ret", "observed PUSH bytes support dispatch even with unknown original register value");
    check(f.analyze(0).back().status == "unresolved", "value budget failure cannot fabricate constructed findings");
    f = {}; f.add({0x21, 0, 0x90}); f.push(0x9000); f.add({0x21, 0xFE, 0x9F}); f.add({0x3E, 0});
    f.add({0x77}, {}, {}, {{DataAccessKind::Write, 0x9FFE, 0}}); f.ret(0x9000);
    check(f.analyze().back().status == "unresolved", "same-value non-PUSH write breaks PUSH byte provenance");
    f.capture.samples[4].stack->accesses[0].kind = DataAccessKind::RefusedWrite;
    check(f.analyze().back().pattern == "pushed_target_ret", "refused overwrite preserves PUSH byte provenance");

    f = {}; f.add({0x21, 0, 0x90}); f.call(); f.add({0xE9}, 0x9000); f.ret(0x8006);
    found = f.analyze();
    check(found[2].pattern == "continuation_preserving_indirect_jump" && found[2].call_sample == "s1" && !found[2].return_role_established,
          "helper primitive preserves caller without asserting function boundaries");
    check(AnalyzeContinuations(f.capture).back().status == "matched", "helper leaves original continuation available to ordinary RET");

    // A call continuation used only to address memory must not confer identity
    // on the unrelated data read from that address, even at equal numeric value.
    f = {}; f.add({0x21, 9, 0x80}); f.add({0x06, 9}); f.add({0x70}); f.call();
    f.pop({0xE1}, 0x8009); f.add({0x6E}, {}, {}, {{DataAccessKind::Read, 0x8009, 9}});
    f.add({0x26, 0x80}); f.add({0xE9}, 0x8009);
    check(f.analyze().back().status == "unresolved", "address dependencies are not data continuation identity");

    f = {}; f.pc = 0x807D; f.call(); f.pop({0xE1}, 0x8080); f.add({0x65}); f.add({0xE9}, 0x8080);
    check(f.analyze().back().status == "unresolved", "equal high and low bytes must retain their own original lanes");
    f = {}; f.call(); f.pop(); f.add({0x31, 0, 0x90}, {}, 0x9000); f.add({0xE9}, 0x8003);
    check(f.analyze().back().status == "unresolved", "unrestored SP prevents ordinary register return classification");

    f = {}; f.call(); f.call(); f.pop({0xE1}, 0x8103); f.add({0xE9}, 0x8103); f.ret(0x8003);
    found = f.analyze();
    check(found[3].return_role_established && found[3].call_sample == "s1", "nested register return selects the live inner invocation");
    check(AnalyzeContinuations(f.capture).back().status == "matched", "outer ordinary return survives inner register return");
    std::cout << (failures ? "FAIL" : "PASS") << ": constructed transfers\n";
    return failures ? 1 : 0;
}
