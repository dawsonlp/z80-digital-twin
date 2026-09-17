// Copyright (c) 2026 Larry Dawson. Licensed under the MIT License (see LICENSE).
#include "stack_analysis.h"
#include <algorithm>
#include <array>
#include <format>
#include <map>
#include <set>

namespace z80::dbg::analysis {
namespace {
uint8_t opcode(const TransferSample& sample) {
    for (auto byte : sample.event.bytes) if (byte != 0xDD && byte != 0xFD) return byte;
    return 0;
}
bool continuous(const TransferSample& p, const TransferSample& s) {
    return p.stack && s.stack && p.stack->complete_data_accesses && s.stack->complete_data_accesses &&
        s.stack->previous == p.id && p.event.next_pc == s.event.start && p.stack->after_sp == s.stack->before_sp;
}
// One byte's source word and lane. Arithmetic may preserve SP affine provenance,
// but never saved continuation identity. Address inputs are not data inputs.
struct Byte {
    std::optional<uint32_t> call, snapshot, literal;
    std::optional<size_t> push;
    uint16_t offset = 0;
    unsigned lane = 0;
    bool operator==(const Byte&) const = default;
};
using Word = std::array<Byte, 2>;
std::optional<Byte> whole(const Word& word) {
    auto low = word[0], high = word[1];
    if (low.lane != 0 || high.lane != 1) return {};
    high.lane = 0;
    return low == high ? std::optional<Byte>(low) : std::nullopt;
}
Word split(Byte b) { b.lane = 0; auto high = b; high.lane = 1; return {b, high}; }
struct Invocation {
    size_t sample;
    unsigned removed = 0; // which bytes have been passed/popped
    std::vector<std::string> removal_samples;
};
struct SavedSP { Byte origin; std::string departure; };
struct Prepared { size_t push, dispatch; uint16_t slot; };
void append(std::vector<std::string>& items, const std::string& item) {
    if (std::find(items.begin(), items.end(), item) == items.end()) items.push_back(item);
}
} // namespace
std::vector<StackFinding> AnalyzeStackReconstruction(const TransferCapture& capture,
    const std::vector<ContinuationFinding>& continuations, const ValueAnalysis& values,
    const std::vector<ConstructedFinding>& constructed) {
    std::map<std::string, size_t> indices;
    for (size_t i = 0; i < capture.samples.size(); ++i) indices.emplace(capture.samples[i].id, i);
    std::vector<Word> origins(values.nodes.size());
    std::vector<std::vector<const ValueNode*>> nodes(capture.samples.size());
    for (const auto& n : values.nodes) {
        const auto index = indices.at(n.sample_id);
        nodes[index].push_back(&n);
        Word word{};
        auto input = [&](size_t i) -> const Word& { return origins.at(n.inputs.at(i).value - 1); };
        if (n.operation == "call_continuation") { Byte b; b.call = uint32_t(index); word = split(b); }
        else if (n.operation == "entry_stack_pointer") { Byte b; b.snapshot = n.id.value; word = split(b); }
        else if (n.operation == "stack_pointer_assignment") {
            auto source = whole(input(0));
            if (source && source->snapshot) word = split(*source);
            else { Byte b; b.snapshot = n.id.value; word = split(b); }
        }
        else if (n.operation == "immediate" && n.width == 16) { Byte b; b.literal = n.id.value; word = split(b); }
        else if (n.operation == "join_le") word = {input(0)[0], input(1)[0]};
        else if (n.operation == "high_byte") word[0] = input(0)[1];
        else if (n.operation == "low_byte") word[0] = input(0)[0];
        else if (n.operation == "register_copy" || n.operation == "exchange") word = input(0);
        else if (n.operation == "memory_read") word[0] = input(0)[0];
        else if (n.operation == "memory_write") {
            word[0] = input(0)[0];
            word[0].push = (opcode(capture.samples[index]) & 0xCF) == 0xC5 ? std::optional<size_t>(index) : std::nullopt;
        } else if (n.operation == "stack_adjust" || n.operation == "inc16" || n.operation == "dec16" || n.operation == "add16") {
            auto source = whole(input(0));
            size_t source_input = 0;
            bool permitted = n.operation != "add16";
            if (!permitted) {
                auto other = whole(input(1));
                if (source && source->snapshot && other && other->literal) permitted = true;
                else if (other && other->snapshot && source && source->literal) {
                    source = other; source_input = 1; permitted = true;
                }
            }
            if (permitted && source && source->snapshot) {
                source->offset = uint16_t(source->offset + n.value - values.nodes.at(n.inputs[source_input].value - 1).value);
                source->push.reset(); word = split(*source);
            }
        }
        origins.at(n.id.value - 1) = word;
    }
    std::vector<Invocation> live;
    std::vector<SavedSP> saved_sp;
    std::vector<Prepared> prepared;
    std::set<size_t> consumed_pushes;
    std::map<uint16_t, const ValueNode*> memory;
    std::vector<StackFinding> result;
    for (size_t i = 0; i < capture.samples.size(); ++i) {
        const auto& sample = capture.samples[i]; const auto& v = values.findings.at(i);
        const auto& c = continuations.at(i); const auto& made = constructed.at(i);
        const auto t = ClassifyTransfer(sample);
        StackFinding f; f.sample_id = sample.id;
        auto clear = [&] { live.clear(); saved_sp.clear(); prepared.clear(); consumed_pushes.clear(); memory.clear(); };
        const bool boundary = !i || !continuous(capture.samples[i - 1], sample);
        if (boundary) clear();
        if (!sample.stack || v.status == ValueStatus::Unresolved || t.mechanism == TransferMechanism::InterruptReturn) {
            clear(); f.status = "unresolved";
            f.unresolved.push_back("continuous supported value and stack effects required for reconstruction");
            result.push_back(std::move(f)); continue;
        }
        const auto& stack = *sample.stack;
        const bool pop = (opcode(sample) & 0xCF) == 0xC1;
        const bool ret = t.mechanism == TransferMechanism::Return && t.taken == true;
        const bool jump = t.mechanism == TransferMechanism::IndirectJump;
        auto match = [&](std::string pattern, std::string explanation) {
            f.status = "matched"; f.pattern = std::move(pattern); f.explanation = std::move(explanation);
        };
        // Track committed writes only. Even equal-value replacement invalidates
        // a previously prepared literal continuation's particular write identity.
        for (const auto* n : nodes[i]) if (n->operation == "memory_write") {
            memory[*n->memory_address] = n;
            std::erase_if(prepared, [&](const auto& p) { return *n->memory_address == p.slot || *n->memory_address == uint16_t(p.slot + 1); });
        }
        if (v.before_sp_root && v.after_sp_root) {
            f.sp_root = v.after_sp_root;
            auto before = whole(origins.at(v.before_sp_root->value - 1));
            auto after = whole(origins.at(v.after_sp_root->value - 1));
            auto restore = saved_sp.end();
            if (after && after->snapshot && stack.before_sp != stack.after_sp) restore = std::find_if(saved_sp.begin(), saved_sp.end(), [&](const auto& p) {
                return p.origin.snapshot == after->snapshot && p.origin.offset == after->offset;
            });
            if (restore != saved_sp.end()) {
                match("restored_stack_pointer", std::format("SP restored to ${:04X} through saved pointer lineage from before sample {}.", stack.after_sp, restore->departure));
                f.supporting_samples.push_back(restore->departure);
                saved_sp.erase(restore, saved_sp.end());
            } else {
                match("stack_pointer_changed", std::format("Observed explicit SP assignment/adjustment from ${:04X} to ${:04X}; saved-pointer restoration is not established.", stack.before_sp, stack.after_sp));
                if (before && before->snapshot && stack.before_sp != stack.after_sp) saved_sp.push_back({*before, sample.id});
            }
        }
        // A POP or one/two-byte upward SP adjustment can remove saved bytes.
        // Larger assignments are accepted as bypass evidence only when both
        // endpoints are known live invocation slots, not arbitrary address order.
        const auto advance = uint16_t(stack.after_sp - stack.before_sp);
        for (auto& call : live) {
            const auto slot = capture.samples[call.sample].stack->after_sp;
            if ((pop || (v.after_sp_root && advance <= 2)) && advance > 0 && advance <= 2) {
                for (unsigned a = 0; a < advance; ++a) {
                    const auto address = uint16_t(stack.before_sp + a);
                    if (address == slot) call.removed |= 1;
                    if (address == uint16_t(slot + 1)) call.removed |= 2;
                }
                if (call.removed && (stack.before_sp == slot || stack.before_sp == uint16_t(slot + 1))) append(call.removal_samples, sample.id);
            }
        }
        if (v.after_sp_root && !live.empty() && stack.before_sp == capture.samples[live.back().sample].stack->after_sp) {
            for (size_t outer = 0; outer < live.size(); ++outer)
                if (stack.after_sp == capture.samples[live[outer].sample].stack->after_sp)
                    for (size_t skipped = outer + 1; skipped < live.size(); ++skipped) {
                        live[skipped].removed = 3; append(live[skipped].removal_samples, sample.id);
                    }
        }
        if (pop) for (const auto& call : live) if (call.removed == 3 &&
            capture.samples[call.sample].stack->after_sp == stack.before_sp) {
            match("continuation_removed_from_stack", "Saved continuation from sample " + capture.samples[call.sample].id +
                  " was popped from its stack slot; its register value may still be used.");
            f.call_sample = capture.samples[call.sample].id;
        }
        const auto identity = v.root ? whole(origins.at(v.root->value - 1)) : std::nullopt;
        bool consumed = false;
        if ((ret || jump) && identity && identity->call) {
            auto found = std::find_if(live.begin(), live.end(), [&](const auto& call) { return call.sample == *identity->call; });
            if (found != live.end()) {
                const auto& call = capture.samples[found->sample];
                const auto resumed_sp = ret ? stack.after_sp : stack.before_sp;
                bool skips_proven = true;
                for (auto next = found + 1; next != live.end(); ++next) skips_proven &= next->removed == 3;
                const bool register_consumed = !jump || found->removed == 3;
                if (resumed_sp == call.stack->before_sp && register_consumed && skips_proven) {
                    if (found + 1 != live.end()) {
                        match("caller_skipping_exit", std::format("Observed exit to ${:04X} through the continuation from sample {}, bypassing explicitly removed inner continuations.", sample.event.next_pc, call.id));
                        for (auto next = found + 1; next != live.end(); ++next) {
                            f.skipped_calls.push_back(capture.samples[next->sample].id);
                            for (const auto& proof : next->removal_samples) append(f.supporting_samples, proof);
                        }
                    } else if (c.status != "matched" && !made.return_role_established) {
                        match("reconstructed_continuation_return", std::format("Observed return to ${:04X} through the reconstructed continuation from sample {}; SP is restored to ${:04X}.", sample.event.next_pc, call.id, resumed_sp));
                    }
                    if (f.status == "matched") { f.call_sample = call.id; f.return_role_established = true; f.target_root = v.root; }
                    live.erase(found, live.end()); consumed = true;
                } else if (ret && found + 1 == live.end()) {
                    match("continuation_on_rebuilt_stack", std::format("RET reached continuation ${:04X} from sample {} on a changed stack; restoration of the caller's SP is not established.", sample.event.next_pc, call.id));
                    f.call_sample = call.id; f.target_root = v.root;
                    f.unresolved.push_back("caller stack context not restored");
                    live.erase(found, live.end()); consumed = true;
                } else {
                    f.status = "unresolved";
                    f.unresolved.push_back("saved continuation found, but stack restoration or removal of inner continuations is unproven");
                }
            }
        }
        if (ret && !consumed) {
            // Confirm a previously prepared literal continuation only when it
            // is actually consumed after the earlier dispatch.
            auto p = prepared.end();
            if (identity && identity->literal && identity->push && !consumed_pushes.contains(*identity->push))
                p = std::find_if(prepared.begin(), prepared.end(), [&](const auto& item) { return item.push == *identity->push && item.slot == stack.before_sp; });
            if (p != prepared.end()) {
                match("constructed_continuation_consumed", std::format("Observed return to explicitly prepared address ${:04X}, retained on the stack across dispatch sample {} and now consumed by RET.", sample.event.next_pc, capture.samples[p->dispatch].id));
                f.supporting_samples = {capture.samples[p->push].id, capture.samples[p->dispatch].id};
                f.target_root = v.root; f.return_role_established = true;
                prepared.erase(p); consumed = true;
            } else if (!live.empty()) {
                const auto& call = capture.samples[live.back().sample];
                const auto l = memory.find(stack.before_sp), h = memory.find(uint16_t(stack.before_sp + 1));
                if (stack.before_sp == call.stack->after_sp && stack.after_sp == call.stack->before_sp &&
                    l != memory.end() && h != memory.end() &&
                    (indices.at(l->second->sample_id) > live.back().sample || indices.at(h->second->sample_id) > live.back().sample)) {
                    match("substituted_continuation_transfer", std::format("RET consumed replacement bytes in the saved continuation slot of sample {}, reaching ${:04X}; original return identity is not established.", call.id, sample.event.next_pc));
                    f.call_sample = call.id; f.target_root = v.root;
                    append(f.supporting_samples, l->second->sample_id); append(f.supporting_samples, h->second->sample_id);
                    live.pop_back(); consumed = true;
                }
            }
        }
        if (pop) {
            const auto l = memory.find(stack.before_sp), h = memory.find(uint16_t(stack.before_sp + 1));
            if (l != memory.end() && h != memory.end()) {
                auto popped = whole(Word{origins[l->second->id.value - 1][0], origins[h->second->id.value - 1][0]});
                if (popped && popped->push) consumed_pushes.insert(*popped->push);
            }
            std::erase_if(prepared, [&](const auto& p) { return p.slot == stack.before_sp; });
        }
        if (ret && identity && identity->push) consumed_pushes.insert(*identity->push);
        if (made.pattern == "pushed_target_ret") {
            const auto l = memory.find(stack.after_sp), h = memory.find(uint16_t(stack.after_sp + 1));
            if (l != memory.end() && h != memory.end()) {
                auto remaining = whole(Word{origins[l->second->id.value - 1][0], origins[h->second->id.value - 1][0]});
                if (remaining && remaining->literal && remaining->push && *remaining->push < i && !consumed_pushes.contains(*remaining->push)) {
                    if (std::none_of(prepared.begin(), prepared.end(), [&](const auto& p) {
                        return p.push == *remaining->push && p.slot == stack.after_sp;
                    })) prepared.push_back({*remaining->push, i, stack.after_sp});
                    if (f.status != "matched") {
                        match("prepared_continuation_candidate", std::format("Stack dispatch leaves explicitly pushed address ${:04X} at SP; later consumption as a continuation has not yet been observed.", uint16_t(l->second->value | (h->second->value << 8))));
                        f.supporting_samples = {capture.samples[*remaining->push].id};
                        f.target_root = v.root;
                    }
                }
            }
        }
        // Ordinary/constructed retirement also applies when this tactic adds no
        // new explanation. Unclassified exits discard uncertain invocation state.
        if (!consumed && (c.status == "matched" || made.return_role_established)) {
            const auto id = c.status == "matched" ? c.call_sample : made.call_sample;
            auto found = std::find_if(live.begin(), live.end(), [&](const auto& call) { return capture.samples[call.sample].id == id; });
            if (found != live.end()) live.erase(found, live.end()); else live.clear();
        } else if (!consumed && ((ret && made.pattern != "pushed_target_ret") ||
                   (jump && made.status != "matched"))) live.clear();
        if (t.mechanism == TransferMechanism::Jump && t.taken == true && !live.empty()) {
            const auto& call = capture.samples[live.back().sample];
            if (live.back().removed && sample.event.next_pc == uint16_t(call.event.start + call.event.bytes.size())) live.clear();
        }
        if (c.status == "created") live.push_back({i, 0, {}});
        result.push_back(std::move(f));
    }
    return result;
}
json::Value StackFindingJson(const StackFinding& f) {
    using J = json::Value;
    auto array = [](const auto& items) { J::Array a; for (const auto& item : items) a.emplace_back(item); return a; };
    return J::Object{{"sample_id", f.sample_id}, {"status", f.status}, {"pattern", f.pattern},
        {"explanation", f.explanation}, {"tactic", std::string(kStackTacticVersion)},
        {"call_sample", f.call_sample ? J(*f.call_sample) : J{}}, {"supporting_samples", array(f.supporting_samples)},
        {"skipped_calls", array(f.skipped_calls)}, {"return_role_established", f.return_role_established},
        {"target_root", f.target_root ? J(f.target_root->value) : J{}}, {"sp_root", f.sp_root ? J(f.sp_root->value) : J{}},
        {"unresolved", array(f.unresolved)}};
}
} // namespace z80::dbg::analysis
