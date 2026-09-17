// Copyright (c) 2026 Larry Dawson. Licensed under the MIT License (see LICENSE).
#include "value_analysis.h"
#include <algorithm>
#include <array>
#include <format>
#include <map>

namespace z80::dbg::analysis {
namespace {
using Node = std::optional<ValueNodeId>;
// Register bytes: BC, DE, HL, AF, IX, IY, BC', DE', HL', AF', SP (high first).
class Analyzer {
    ValueAnalysis out_;
    std::array<Node, 22> registers_{};
    std::map<uint16_t, ValueNodeId> memory_;
    const TransferSample* current_ = nullptr;
    std::vector<bool> used_;
    std::string failure_;

    const ValueNode& node(ValueNodeId id) const { return out_.nodes.at(id.value - 1); }
    uint16_t value(Node n) const { return n ? node(*n).value : 0; }
    void fail(std::string reason) { if (failure_.empty()) failure_ = std::move(reason); }
    void clear() { registers_.fill({}); memory_.clear(); }
    Node make(std::string op, uint16_t v, uint8_t width, std::vector<ValueNodeId> inputs = {},
              bool complete = true, std::optional<uint16_t> address = {}) {
        if (out_.nodes.size() >= out_.node_budget) {
            out_.exhausted = true; fail("value-node budget exhausted"); return {};
        }
        for (auto input : inputs) complete &= node(input).complete;
        ValueNodeId id{static_cast<uint32_t>(out_.nodes.size() + 1)};
        out_.nodes.push_back({id, current_->id, std::move(op), v, width, complete, address, std::move(inputs)});
        return id;
    }
    Node word(int hi) {
        if (!registers_[hi] || !registers_[hi + 1]) return {};
        return make("join_le", uint16_t((value(registers_[hi]) << 8) | value(registers_[hi + 1])), 16,
                    {*registers_[hi + 1], *registers_[hi]});
    }
    void put_word(int hi, Node n) {
        if (!n) { registers_[hi].reset(); registers_[hi + 1].reset(); return; }
        registers_[hi] = make("high_byte", uint8_t(value(n) >> 8), 8, {*n});
        registers_[hi + 1] = make("low_byte", uint8_t(value(n)), 8, {*n});
    }
    void entry_byte(int reg, uint8_t v) {
        if (registers_[reg] && value(registers_[reg]) != v) {
            fail("captured entry register contradicts value lineage"); return;
        }
        if (!registers_[reg]) registers_[reg] = make("entry_register", v, 8, {}, false);
    }
    void entry_word(int hi, std::optional<uint16_t> v) {
        if (v) { entry_byte(hi, uint8_t(*v >> 8)); entry_byte(hi + 1, uint8_t(*v)); }
    }
    void exchange(int a, int b) {
        const auto left = registers_[a], right = registers_[b];
        registers_[a] = right ? make("exchange", value(right), 8, {*right}) : Node{};
        registers_[b] = left ? make("exchange", value(left), 8, {*left}) : Node{};
    }
    const DataAccess* access(DataAccessKind kind, uint16_t address) {
        const auto& accesses = current_->stack->accesses;
        for (size_t i = 0; i < accesses.size(); ++i)
            if (!used_[i] && accesses[i].kind == kind && accesses[i].address == address) {
                used_[i] = true; return &accesses[i];
            }
        fail("required data access missing"); return nullptr;
    }
    Node read(uint16_t address, Node address_origin = {}) {
        const auto* a = access(DataAccessKind::Read, address);
        if (!a) return {};
        std::vector<ValueNodeId> inputs;
        bool known = false;
        if (const auto m = memory_.find(address); m != memory_.end()) {
            if (node(m->second).value != a->value) { fail("memory read contradicts previous observed value"); return {}; }
            inputs.push_back(m->second); known = true;
        }
        if (address_origin) inputs.push_back(*address_origin);
        auto result = make(known ? "memory_read" : "memory_read_before_trace", a->value, 8, std::move(inputs), known, address);
        // Remember the observed value even without an earlier writer; subsequent
        // writes create new nodes, including same-value writes.
        if (result) memory_.insert_or_assign(address, *result);
        return result;
    }
    void write(uint16_t address, Node origin, Node address_origin = {}) {
        const DataAccess* a = nullptr;
        const auto& accesses = current_->stack->accesses;
        bool refused = false;
        for (size_t i = 0; i < accesses.size(); ++i)
            if (!used_[i] && accesses[i].address == address && accesses[i].kind != DataAccessKind::Read) {
                used_[i] = true; a = &accesses[i]; refused = a->kind == DataAccessKind::RefusedWrite; break;
            }
        if (!a) { fail("required write access missing"); return; }
        if (origin && value(origin) != a->value) { fail("write value contradicts source register"); return; }
        if (refused) return;
        if (!origin) origin = make("untraced_register", a->value, 8, {}, false);
        if (!origin) return;
        std::vector<ValueNodeId> inputs{*origin};
        if (address_origin) inputs.push_back(*address_origin);
        auto n = make("memory_write", a->value, 8, std::move(inputs), true, address);
        if (n) memory_.insert_or_assign(address, *n);
    }
    Node read_word(uint16_t address, Node address_origin = {}) {
        auto low = read(address, address_origin), high = read(uint16_t(address + 1), address_origin);
        if (!low || !high) return {};
        return make("join_le", uint16_t(value(low) | (value(high) << 8)), 16, {*low, *high});
    }
    void write_word(uint16_t address, Node origin, Node address_origin = {}) {
        Node low, high;
        if (origin) {
            low = make("low_byte", uint8_t(value(origin)), 8, {*origin});
            high = make("high_byte", uint8_t(value(origin) >> 8), 8, {*origin});
        }
        write(address, low, address_origin); write(uint16_t(address + 1), high, address_origin);
    }
    void target(ValueFinding& finding, Node origin) {
        if (!origin) { finding.status = ValueStatus::Unresolved; finding.unresolved.push_back("target value origin unavailable"); return; }
        if (value(origin) != current_->event.next_pc) { fail("traced target disagrees with observed PC"); return; }
        finding.root = origin;
        finding.status = node(*origin).complete ? ValueStatus::Traced : ValueStatus::Partial;
        finding.explanation = std::format("Observed target ${:04X} follows value node {}; inspect its inputs for source observations.", value(origin), origin->value);
        if (!node(*origin).complete) finding.unresolved.push_back("value chain reaches entry context or memory predating the retained trace");
    }
    bool effect_consistent(const TransferFinding& f) const {
        for (const auto& reason : f.unresolved)
            if (reason != "stack effects and continuation relationship not analyzed" &&
                reason != "target register value origin not traced" &&
                reason != "target register not captured; value origin unresolved") return false;
        return f.mechanism != TransferMechanism::Unknown && f.mechanism != TransferMechanism::MachineTransition;
    }
    void execute(ValueFinding& finding, const TransferFinding& transfer) {
        const auto& e = current_->event;
        const auto& s = *current_->stack;
        size_t i = 0; uint8_t prefix = 0;
        while (i < e.bytes.size() && (e.bytes[i] == 0xDD || e.bytes[i] == 0xFD)) prefix = e.bytes[i++];
        const auto op = e.bytes.at(i++);
        const int hl = prefix == 0xDD ? 8 : prefix == 0xFD ? 10 : 4;
        const auto rp = [&](unsigned n) { return n == 2 ? hl : n == 3 ? 20 : int(n * 2); };
        const auto byte_reg = [&](unsigned n) { return n == 7 ? 6 : n == 4 ? hl : n == 5 ? hl + 1 : int(n); };
        const auto nn = [&] { return uint16_t(e.bytes.at(i) | (uint16_t(e.bytes.at(i + 1)) << 8)); };
        uint16_t expected_sp = s.before_sp;
        const bool explicit_sp = op == 0x31 || op == 0xF9 || op == 0x33 || op == 0x3B ||
            (op == 0xED && e.bytes.at(i) == 0x7B);
        if (explicit_sp) finding.before_sp_root = word(20);
        auto adjust_sp = [&] {
            const auto old = word(20);
            put_word(20, old ? make("stack_adjust", expected_sp, 16, {*old}) : Node{});
        };
        const auto invalidate_flags = [&] {
            registers_[7].reset();
            finding.explanation = "Known instruction effects preserve unrelated value lineage; resulting flags are not traced.";
            finding.unresolved.push_back("resulting flag value lineage not modeled; later captured flags are a new evidence boundary");
        };
        const auto memory_operand = [&](int pair, int displacement) -> Node {
            const auto base = word(pair);
            if (!base) { fail("address unavailable for memory operand"); return {}; }
            const auto address = displacement == 0 ? base :
                make("indexed_address", uint16_t(value(base) + displacement), 16, {*base});
            return address ? read(value(address), address) : Node{};
        };
        if (transfer.mechanism == TransferMechanism::Call || transfer.mechanism == TransferMechanism::Restart) {
            if (transfer.taken == true) {
                expected_sp = uint16_t(s.before_sp - 2);
                auto continuation = make("call_continuation", uint16_t(e.start + e.bytes.size()), 16);
                write_word(expected_sp, continuation); adjust_sp();
            }
        } else if (transfer.mechanism == TransferMechanism::Return || transfer.mechanism == TransferMechanism::InterruptReturn) {
            if (transfer.taken == true) {
                target(finding, read_word(s.before_sp)); expected_sp = uint16_t(s.before_sp + 2);
                adjust_sp();
            }
        } else if (transfer.mechanism == TransferMechanism::IndirectJump) {
            target(finding, word(hl));
        } else if ((op & 0xCF) == 0x01) {
            const auto pair = rp((op >> 4) & 3);
            put_word(pair, make("immediate", nn(), 16));
            if (pair == 20) expected_sp = nn();
        } else if ((op & 0xCF) == 0xC1 || (op & 0xCF) == 0xC5) {
            const auto pair = ((op >> 4) & 3) == 3 ? 6 : rp((op >> 4) & 3);
            if ((op & 0xCF) == 0xC1) {
                put_word(pair, read_word(s.before_sp)); expected_sp = uint16_t(s.before_sp + 2);
            } else { expected_sp = uint16_t(s.before_sp - 2); write_word(expected_sp, word(pair)); }
            adjust_sp();
        } else if (op == 0xEB) {
            exchange(2, 4); exchange(3, 5);
        } else if (op == 0xD9) {
            for (int r = 0; r < 6; ++r) exchange(r, r + 12);
        } else if (op == 0x08) {
            exchange(6, 18); exchange(7, 19);
        } else if (op == 0xE3) {
            const auto old = word(hl); const auto replacement = read_word(s.before_sp);
            write_word(s.before_sp, old); put_word(hl, replacement);
        } else if (op == 0x22 || op == 0x2A) {
            const auto address = make("immediate_address", nn(), 16);
            if (op == 0x2A) put_word(hl, read_word(nn(), address));
            else write_word(nn(), word(hl), address);
        } else if (op == 0xED && ((e.bytes.at(i) & 0xCF) == 0x43 || (e.bytes.at(i) & 0xCF) == 0x4B)) {
            const auto extension = e.bytes.at(i++);
            const auto pair = ((extension >> 4) & 3) == 2 ? 4 : rp((extension >> 4) & 3);
            const auto address = make("immediate_address", nn(), 16);
            if ((extension & 0xCF) == 0x4B) put_word(pair, read_word(nn(), address));
            else write_word(nn(), word(pair), address);
            if (pair == 20 && (extension & 0xCF) == 0x4B) expected_sp = value(word(20));
        } else if ((op & 0xCF) == 0x09) {
            const auto left = word(hl), right = word(rp((op >> 4) & 3));
            put_word(hl, left && right ? make("add16", uint16_t(value(left) + value(right)), 16, {*left, *right}) : Node{});
            registers_[7].reset();
        } else if ((op & 0xC7) == 0x03) {
            const auto pair = rp((op >> 4) & 3); const auto old = word(pair);
            const bool decrement = (op & 8) != 0;
            const auto next = old ? make(decrement ? "dec16" : "inc16", uint16_t(value(old) + (decrement ? -1 : 1)), 16, {*old}) : Node{};
            put_word(pair, next);
            if (pair == 20) expected_sp = value(next);
        } else if (!prefix && op >= 0x40 && op <= 0x7F && op != 0x76) {
            const unsigned src = op & 7, dst = (op >> 3) & 7;
            if (src == 6 || dst == 6) {
                const auto address = word(4);
                if (!address) fail("HL address unavailable for memory operand");
                else if (src == 6) registers_[byte_reg(dst)] = read(value(address), address);
                else write(value(address), registers_[byte_reg(src)], address);
            } else {
                const auto input = registers_[byte_reg(src)];
                registers_[byte_reg(dst)] = input ? make("register_copy", value(input), 8, {*input}) : Node{};
            }
        } else if ((op & 0xC7) == 0x06 && ((op >> 3) & 7) != 6) {
            registers_[byte_reg((op >> 3) & 7)] = make("immediate", e.bytes.at(i), 8);
        } else if (!prefix && ((op & 0xC7) == 0x04 || (op & 0xC7) == 0x05) && ((op >> 3) & 7) != 6) {
            const int reg = byte_reg((op >> 3) & 7); const auto old = registers_[reg];
            registers_[reg] = old ? make((op & 1) ? "dec8" : "inc8", uint8_t(value(old) + ((op & 1) ? -1 : 1)), 8, {*old}) : Node{};
            registers_[7].reset();
        } else if (op == 0xCB &&
                   (e.bytes.at(i + (prefix ? 1 : 0)) & 0xC0) == 0x40) {
            // BIT reads its operand but changes only flags, including indexed
            // encodings whose low opcode bits do not select a destination.
            const auto extension = e.bytes.at(i + (prefix ? 1 : 0));
            if (prefix) memory_operand(hl, int8_t(e.bytes.at(i)));
            else if ((extension & 7) == 6) memory_operand(4, 0);
            invalidate_flags();
        } else if ((op >= 0xB8 && op <= 0xBF) || op == 0xFE) {
            // CP leaves both operands intact. Their values are unnecessary
            // here because flags are deliberately not evaluated by this tactic.
            if (op == 0xBE) memory_operand(hl, prefix ? int8_t(e.bytes.at(i)) : 0);
            invalidate_flags();
        } else if (op == 0x37 || op == 0x3F) {
            invalidate_flags(); // SCF / CCF leave all data registers intact.
        } else if ((op >= 0xA0 && op <= 0xB7) || op == 0xE6 || op == 0xEE || op == 0xF6) {
            const unsigned operation = (op >> 3) & 3;
            Node operand;
            if (op >= 0xE6) operand = make("immediate", e.bytes.at(i), 8);
            else if ((op & 7) == 6) operand = memory_operand(hl, prefix ? int8_t(e.bytes.at(i)) : 0);
            else operand = registers_[byte_reg(op & 7)];
            const auto accumulator = registers_[6];
            if (accumulator && operand) {
                const auto left = value(accumulator), right = value(operand);
                registers_[6] = make(operation == 0 ? "and8" : operation == 1 ? "xor8" : "or8",
                    uint8_t(operation == 0 ? left & right : operation == 1 ? left ^ right : left | right),
                    8, {*accumulator, *operand});
            } else {
                registers_[6].reset();
                finding.unresolved.push_back("accumulator result lineage unavailable because a logical operand is unknown");
            }
            invalidate_flags();
        } else if (op == 0xF9) {
            const auto source = word(hl);
            if (!source) fail("source for LD SP unavailable");
            else { expected_sp = value(source); put_word(20, source); }
        } else if (transfer.mechanism == TransferMechanism::Jump) {
            if (op == 0x10) {
                const auto old = registers_[0];
                registers_[0] = old ? make("dec8", uint8_t(value(old) - 1), 8, {*old}) : Node{};
            }
        } else if (op != 0x00 && op != 0x76 && op != 0xF3 && op != 0xFB) {
            fail(std::format("unsupported value operation ${:02X}; prior lineage discarded", op));
        }
        if (explicit_sp) {
            const auto assigned = word(20);
            if (assigned) put_word(20, make("stack_pointer_assignment", expected_sp, 16, {*assigned}));
            finding.after_sp_root = word(20);
        }
        if (expected_sp != s.after_sp) fail("stack movement contradicts supported instruction effect");
        if (std::find(used_.begin(), used_.end(), false) != used_.end()) fail("unexplained data accesses; prior lineage discarded");
    }
public:
    explicit Analyzer(size_t budget) { out_.node_budget = std::min(budget, kMaxValueNodes); }
    ValueAnalysis run(const TransferCapture& capture) {
        const TransferSample* previous = nullptr;
        for (const auto& sample : capture.samples) {
            current_ = &sample; failure_.clear();
            const auto checkpoint = out_.nodes.size();
            ValueFinding finding; finding.sample_id = sample.id;
            const auto transfer = ClassifyTransfer(sample);
            const bool continuous = previous && sample.stack && previous->stack && sample.stack->previous == previous->id &&
                previous->event.next_pc == sample.event.start && previous->stack->after_sp == sample.stack->before_sp &&
                previous->stack->complete_data_accesses;
            if (!continuous) clear();
            if (out_.exhausted) fail("value-node budget exhausted");
            else if (!sample.stack || !sample.stack->complete_data_accesses || !effect_consistent(transfer))
                fail("complete consistent instruction and data-access evidence required for value tracing");
            else {
                if (sample.before.flags) entry_byte(7, *sample.before.flags);
                if (sample.before.b) entry_byte(0, *sample.before.b);
                entry_word(4, sample.before.hl); entry_word(8, sample.before.ix); entry_word(10, sample.before.iy);
                if (!registers_[20] && !registers_[21])
                    put_word(20, make("entry_stack_pointer", sample.stack->before_sp, 16, {}, false));
                else entry_word(20, sample.stack->before_sp);
                used_.assign(sample.stack->accesses.size(), false);
                if (failure_.empty()) execute(finding, transfer);
            }
            if (!failure_.empty()) {
                out_.nodes.resize(checkpoint); // Do not publish nodes from a failed effect.
                clear(); finding.root.reset(); finding.before_sp_root.reset(); finding.after_sp_root.reset(); finding.status = ValueStatus::Unresolved;
                finding.explanation.clear(); finding.unresolved.push_back(failure_);
            }
            out_.findings.push_back(std::move(finding)); previous = &sample;
        }
        return std::move(out_);
    }
};
} // namespace
ValueAnalysis AnalyzeAddressValues(const TransferCapture& capture, size_t node_budget) {
    return Analyzer(node_budget).run(capture);
}
json::Value ValueFindingJson(const ValueFinding& finding) {
    using J = json::Value;
    constexpr std::array names = {"not_applicable", "traced", "partial", "unresolved"};
    J::Array unresolved;
    for (const auto& reason : finding.unresolved) unresolved.emplace_back(reason);
    return J::Object{{"sample_id", finding.sample_id}, {"status", names.at(size_t(finding.status))},
        {"root", finding.root ? J(finding.root->value) : J{}},
        {"before_sp_root", finding.before_sp_root ? J(finding.before_sp_root->value) : J{}},
        {"after_sp_root", finding.after_sp_root ? J(finding.after_sp_root->value) : J{}}, {"explanation", finding.explanation},
        {"unresolved", std::move(unresolved)}, {"tactic", std::string(kValueTacticVersion)}};
}
json::Value ValueGraphJson(const ValueAnalysis& analysis) {
    using J = json::Value;
    J::Array nodes;
    for (const auto& n : analysis.nodes) {
        J::Array inputs;
        for (auto id : n.inputs) inputs.emplace_back(id.value);
        nodes.emplace_back(J::Object{{"id", n.id.value}, {"sample_id", n.sample_id}, {"operation", n.operation},
            {"value", int(n.value)}, {"width", int(n.width)}, {"complete", n.complete},
            {"memory_address", n.memory_address ? J(int(*n.memory_address)) : J{}}, {"inputs", std::move(inputs)}});
    }
    return J::Object{{"tactic", std::string(kValueTacticVersion)}, {"node_budget", uint32_t(analysis.node_budget)},
        {"exhausted", analysis.exhausted}, {"nodes", std::move(nodes)}};
}
} // namespace z80::dbg::analysis
