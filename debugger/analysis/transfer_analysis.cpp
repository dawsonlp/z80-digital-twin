// Copyright (c) 2026 Larry Dawson. Licensed under the MIT License (see LICENSE).
#include "transfer_analysis.h"
#include "value_analysis.h"
#include "content_hash.h"
#include <array>
#include <charconv>
#include <set>
#include <tuple>

namespace z80::dbg::analysis {
namespace {
using J = json::Value;
using O = J::Object;
using A = J::Array;
auto invalid(std::string message) { return std::unexpected(Error{ErrorCode::Invalid, std::move(message)}); }
bool condition(unsigned code, uint8_t flags) {
    constexpr std::array<uint8_t, 4> masks = {0x40, 0x01, 0x04, 0x80};
    return ((flags & masks[code / 2]) != 0) == ((code & 1) != 0);
}
std::string mechanism_name(TransferMechanism mechanism) {
    constexpr std::array names = {"unknown", "sequential", "jump", "indirect_jump", "call",
        "restart", "return_instruction", "interrupt_return_instruction", "repeat", "halt", "machine_transition"};
    return names.at(static_cast<size_t>(mechanism));
}
uint64_t unsigned_text(const J& j) {
    const auto& s = j.string();
    uint64_t n = 0;
    const auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), n);
    if (s.empty() || ec != std::errc{} || end != s.data() + s.size() || std::to_string(n) != s)
        throw std::invalid_argument("expected canonical unsigned decimal string");
    return n;
}
uint16_t bounded(const J& j, uint16_t maximum = 65535) {
    const auto n = j.integer();
    if (n < 0 || n > maximum) throw std::invalid_argument("capture integer outside range");
    return static_cast<uint16_t>(n);
}
template<class T> J optional_number(const std::optional<T>& value) {
    return value ? J(int(*value)) : J{};
}
Result<void> validate(const TransferCapture& capture) {
    if (capture.source.empty() || capture.source.size() > 4096 || capture.limitations.empty() ||
        capture.limitations.size() > 65536 || capture.samples.size() > kMaxTransferSamples)
        return invalid("capture needs source, explicit limitations and at most 8192 samples");
    std::set<std::string> ids;
    for (const auto& sample : capture.samples) {
        const auto& e = sample.event;
        if (sample.id.empty() || sample.id.size() > 4096 || !ids.insert(sample.id).second)
            return invalid("sample IDs must be nonempty and unique within capture");
        if (sample.stack) {
            if (sample.stack->accesses.size() > kMaxDataAccesses ||
                (sample.stack->previous && (sample.stack->previous->empty() || sample.stack->previous->size() > 4096 ||
                                           *sample.stack->previous == sample.id)))
                return invalid("invalid stack evidence size or predecessor");
            for (const auto& access : sample.stack->accesses)
                if (access.kind < DataAccessKind::Read || access.kind > DataAccessKind::RefusedWrite)
                    return invalid("invalid data access kind");
        }
        if (e.bytes.size() > MetadataMemory::kCaptureLimit || e.read_count < e.bytes.size() ||
            (!e.revisions.empty() && e.revisions.size() != e.bytes.size()))
            return invalid("invalid captured byte counts or revisions");
        if (e.kind != ObservationKind::Instruction && e.kind != ObservationKind::MachineTransition)
            return invalid("unknown observation kind");
        if (e.kind == ObservationKind::Instruction && e.complete_capture &&
            (e.bytes.empty() || e.bytes.size() != e.read_count))
            return invalid("complete capture must contain every instruction read");
        if (e.kind == ObservationKind::MachineTransition && (!e.bytes.empty() || e.read_count || e.complete_capture))
            return invalid("machine transition must not claim instruction capture");
    }
    return {};
}
J capture_json(const TransferCapture& capture) {
    A samples;
    for (const auto& s : capture.samples) {
        const auto& e = s.event;
        A bytes, revisions;
        for (auto b : e.bytes) bytes.emplace_back(int(b));
        for (auto revision : e.revisions) revisions.emplace_back(std::to_string(revision));
        J stack;
        if (s.stack) {
            A accesses;
            for (const auto& a : s.stack->accesses)
                accesses.emplace_back(O{{"kind", a.kind == DataAccessKind::Read ? "read" :
                    a.kind == DataAccessKind::Write ? "write" : "refused_write"},
                    {"address", int(a.address)}, {"value", int(a.value)}});
            stack = O{{"before_sp", int(s.stack->before_sp)}, {"after_sp", int(s.stack->after_sp)},
                {"complete_data_accesses", s.stack->complete_data_accesses},
                {"previous", s.stack->previous ? J(*s.stack->previous) : J{}}, {"accesses", std::move(accesses)}};
        }
        samples.emplace_back(O{{"id", s.id},
            {"kind", e.kind == ObservationKind::Instruction ? "instruction" : "machine_transition"},
            {"sequence", std::to_string(e.sequence)}, {"start", int(e.start)}, {"next_pc", int(e.next_pc)},
            {"cycles", std::to_string(e.cycles)}, {"read_count", std::to_string(e.read_count)},
            {"complete_capture", e.complete_capture}, {"bytes", std::move(bytes)}, {"revisions", std::move(revisions)},
            {"before", O{{"flags", optional_number(s.before.flags)}, {"b", optional_number(s.before.b)},
                {"hl", optional_number(s.before.hl)}, {"ix", optional_number(s.before.ix)},
                {"iy", optional_number(s.before.iy)}}}, {"stack", std::move(stack)}});
    }
    return O{{"format", "z80-transfer-capture"}, {"version", 2}, {"source", capture.source},
        {"limitations", capture.limitations}, {"samples", std::move(samples)}};
}
} // namespace

TransferFinding ClassifyTransfer(const TransferSample& sample) {
    TransferFinding result;
    result.sample_id = sample.id;
    const auto& e = sample.event;
    if (e.kind == ObservationKind::MachineTransition) {
        result.mechanism = TransferMechanism::MachineTransition;
        result.target_basis = "observed_pc";
        result.unresolved.push_back("machine transition cause not established");
        return result;
    }
    if (!e.complete_capture || e.bytes.empty() || e.bytes.size() > MetadataMemory::kCaptureLimit ||
        e.read_count != e.bytes.size()) {
        result.unresolved.push_back("instruction byte capture incomplete");
        return result;
    }
    const auto read = [&](uint16_t address) { return e.bytes.at(uint16_t(address - e.start)); };
    const auto decoded = Disassembler{}.Decode(read, e.start, {}, static_cast<uint32_t>(e.bytes.size()));
    if (!decoded.complete || decoded.length != e.bytes.size()) {
        result.unresolved.push_back("captured bytes do not describe exactly one complete instruction");
        return result;
    }
    size_t i = 0;
    uint8_t index = 0;
    while (i < e.bytes.size() && (e.bytes[i] == 0xDD || e.bytes[i] == 0xFD)) index = e.bytes[i++];
    if (i == e.bytes.size()) {
        result.unresolved.push_back("prefix sequence has no opcode");
        return result;
    }
    const auto opcode = e.bytes[i];
    const auto sequential = static_cast<uint16_t>(e.start + decoded.length);
    auto conditional = [&](unsigned code) {
        if (sample.before.flags) result.taken = condition(code, *sample.before.flags);
        else result.unresolved.push_back("entry flags not captured; branch outcome unresolved");
    };
    result.encoded_target = decoded.branch_target;
    result.target_basis = decoded.branch_target ? "encoded_operand" : "observed_pc";
    result.mechanism = TransferMechanism::Sequential;
    if (opcode == 0xCD || (opcode & 0xC7) == 0xC4) {
        result.mechanism = TransferMechanism::Call;
        if (opcode == 0xCD) result.taken = true; else conditional((opcode >> 3) & 7);
    } else if ((opcode & 0xC7) == 0xC7) {
        result.mechanism = TransferMechanism::Restart;
        result.taken = true;
    } else if (opcode == 0xC9 || (opcode & 0xC7) == 0xC0) {
        result.mechanism = TransferMechanism::Return;
        result.target_basis = "stack_origin_unresolved";
        if (opcode == 0xC9) result.taken = true; else conditional((opcode >> 3) & 7);
    } else if (opcode == 0xC3 || (opcode & 0xC7) == 0xC2 || opcode == 0x18 ||
               (opcode & 0xE7) == 0x20 || opcode == 0x10) {
        result.mechanism = TransferMechanism::Jump;
        if (opcode == 0xC3 || opcode == 0x18) result.taken = true;
        else if (opcode == 0x10) {
            if (sample.before.b) result.taken = static_cast<uint8_t>(*sample.before.b - 1) != 0;
            else result.unresolved.push_back("entry B not captured; DJNZ outcome unresolved");
        } else conditional(opcode < 0x40 ? ((opcode >> 3) & 3) : ((opcode >> 3) & 7));
    } else if (opcode == 0xE9) {
        result.mechanism = TransferMechanism::IndirectJump;
        result.taken = true;
        const auto value = index == 0xDD ? sample.before.ix : index == 0xFD ? sample.before.iy : sample.before.hl;
        result.target_basis = value ? "entry_register" : "register_value_not_captured";
        if (!value) result.unresolved.push_back("target register not captured; value origin unresolved");
        else if (*value != e.next_pc) result.unresolved.push_back("observed PC disagrees with entry target register");
        else result.unresolved.push_back("target register value origin not traced");
    } else if (opcode == 0x76) {
        result.mechanism = TransferMechanism::Halt;
    } else if (opcode == 0xED && i + 1 < e.bytes.size()) {
        const auto extension = e.bytes[i + 1];
        if ((extension & 0xC7) == 0x45) {
            result.mechanism = TransferMechanism::InterruptReturn;
            result.target_basis = "stack_origin_unresolved";
            result.taken = true;
        } else if ((extension & 0xF4) == 0xB0) {
            result.mechanism = TransferMechanism::Repeat;
            // Repeat instructions rewind two bytes, including after ignored prefixes.
            const auto repeated = static_cast<uint16_t>(sequential - 2);
            if (e.next_pc == repeated) result.taken = true;
            else if (e.next_pc == sequential) result.taken = false;
            else result.unresolved.push_back("repeat successor is inconsistent with instruction");
        }
    }
    if (result.mechanism == TransferMechanism::Return || result.mechanism == TransferMechanism::InterruptReturn ||
        result.mechanism == TransferMechanism::Call || result.mechanism == TransferMechanism::Restart) {
        if (!result.taken || *result.taken)
            result.unresolved.push_back("stack effects and continuation relationship not analyzed");
    }
    std::optional<uint16_t> expected;
    if (result.taken == false || result.mechanism == TransferMechanism::Sequential || result.mechanism == TransferMechanism::Halt)
        expected = sequential;
    else if (result.taken == true && result.encoded_target) expected = result.encoded_target;
    if (expected && *expected != e.next_pc)
        result.unresolved.push_back("observed PC disagrees with decoded effect and entry context");
    return result;
}

Result<std::string> WriteTransferCapture(const TransferCapture& capture) {
    if (auto valid = validate(capture); !valid) return std::unexpected(valid.error());
    auto text = json::Write(capture_json(capture));
    if (text.size() > 16 * 1024 * 1024) return invalid("capture exceeds 16 MiB storage limit");
    return text;
}
Result<TransferCapture> ReadTransferCapture(std::string_view text) {
    try {
        const auto root = json::Parse(text);
        json::Keys(root, {"format", "version", "source", "limitations", "samples"});
        const auto version = root.at("version").integer();
        if (root.at("format").string() != "z80-transfer-capture" || (version != 1 && version != 2))
            return invalid("unsupported transfer capture format/version");
        TransferCapture capture{root.at("source").string(), root.at("limitations").string(), {}};
        if (root.at("samples").array().size() > kMaxTransferSamples) return invalid("too many samples");
        for (const auto& row : root.at("samples").array()) {
            if (version == 1)
                json::Keys(row, {"id", "kind", "sequence", "start", "next_pc", "cycles", "read_count", "complete_capture", "bytes", "revisions", "before"});
            else
                json::Keys(row, {"id", "kind", "sequence", "start", "next_pc", "cycles", "read_count", "complete_capture", "bytes", "revisions", "before", "stack"});
            TransferSample sample;
            sample.id = row.at("id").string();
            auto& e = sample.event;
            const auto& kind = row.at("kind").string();
            if (kind != "instruction" && kind != "machine_transition") return invalid("unknown observation kind");
            e.kind = kind == "instruction" ? ObservationKind::Instruction : ObservationKind::MachineTransition;
            e.sequence = unsigned_text(row.at("sequence"));
            e.start = bounded(row.at("start")); e.next_pc = bounded(row.at("next_pc"));
            e.cycles = unsigned_text(row.at("cycles")); e.read_count = unsigned_text(row.at("read_count"));
            e.complete_capture = row.at("complete_capture").boolean();
            for (const auto& b : row.at("bytes").array()) e.bytes.push_back(static_cast<uint8_t>(bounded(b, 255)));
            for (const auto& r : row.at("revisions").array()) e.revisions.push_back(unsigned_text(r));
            const auto& before = row.at("before");
            json::Keys(before, {"flags", "b", "hl", "ix", "iy"});
            if (!before.at("flags").null()) sample.before.flags = static_cast<uint8_t>(bounded(before.at("flags"), 255));
            if (!before.at("b").null()) sample.before.b = static_cast<uint8_t>(bounded(before.at("b"), 255));
            if (!before.at("hl").null()) sample.before.hl = bounded(before.at("hl"));
            if (!before.at("ix").null()) sample.before.ix = bounded(before.at("ix"));
            if (!before.at("iy").null()) sample.before.iy = bounded(before.at("iy"));
            if (version == 2 && !row.at("stack").null()) {
                const auto& s = row.at("stack");
                json::Keys(s, {"before_sp", "after_sp", "complete_data_accesses", "previous", "accesses"});
                StackEvidence stack;
                stack.before_sp = bounded(s.at("before_sp")); stack.after_sp = bounded(s.at("after_sp"));
                stack.complete_data_accesses = s.at("complete_data_accesses").boolean();
                if (!s.at("previous").null()) stack.previous = s.at("previous").string();
                if (s.at("accesses").array().size() > kMaxDataAccesses) return invalid("too many data accesses");
                for (const auto& a : s.at("accesses").array()) {
                    json::Keys(a, {"kind", "address", "value"});
                    const auto& kind = a.at("kind").string();
                    if (kind != "read" && kind != "write" && kind != "refused_write") return invalid("unknown data access kind");
                    stack.accesses.push_back({kind == "read" ? DataAccessKind::Read : kind == "write" ? DataAccessKind::Write : DataAccessKind::RefusedWrite,
                                             bounded(a.at("address")), uint8_t(bounded(a.at("value"), 255))});
                }
                sample.stack = std::move(stack);
            }
            capture.samples.push_back(std::move(sample));
        }
        if (auto valid = validate(capture); !valid) return std::unexpected(valid.error());
        return capture;
    } catch (const std::invalid_argument& error) { return invalid(error.what()); }
}

Result<std::string> TransferReport(const TransferCapture& capture) {
    if (auto valid = validate(capture); !valid) return std::unexpected(valid.error());
    A occurrences, edges;
    using Key = std::tuple<uint16_t, std::vector<uint8_t>, TransferMechanism, uint16_t, int>;
    std::map<Key, A> grouped;
    const auto continuations = AnalyzeContinuations(capture);
    const auto values = AnalyzeAddressValues(capture);
    for (size_t i = 0; i < capture.samples.size(); ++i) {
        const auto& s = capture.samples[i];
        const auto& continuation = continuations[i];
        const auto& origin = values.findings[i];
        const auto f = ClassifyTransfer(s);
        auto target_basis = f.target_basis;
        if (origin.status == ValueStatus::Traced) target_basis = "traced_value_origin";
        if (origin.status == ValueStatus::Partial) target_basis = "partially_traced_value_origin";
        if (continuation.status == "matched") target_basis = "matched_call_continuation";
        A unresolved;
        for (const auto& reason : f.unresolved) {
            if (reason == "stack effects and continuation relationship not analyzed" && s.stack) continue;
            if (origin.root && (reason == "target register value origin not traced" ||
                               reason == "target register not captured; value origin unresolved")) continue;
            unresolved.emplace_back(reason);
        }
        for (const auto& reason : continuation.unresolved) unresolved.emplace_back(reason);
        for (const auto& reason : origin.unresolved) unresolved.emplace_back(reason);
        occurrences.emplace_back(O{{"sample_id", s.id}, {"mechanism", mechanism_name(f.mechanism)},
            {"taken", f.taken ? J(*f.taken) : J{}}, {"encoded_target", optional_number(f.encoded_target)},
            {"observed_next_pc", int(s.event.next_pc)}, {"target_basis", target_basis},
            {"instruction_effect", O{{"tactic", std::string(kTransferTacticVersion)}, {"target_basis", f.target_basis}}},
            {"value_origin", ValueFindingJson(origin)},
            {"continuation", O{{"status", continuation.status},
                {"call_sample", continuation.call_sample ? J(*continuation.call_sample) : J{}},
                {"explanation", continuation.explanation}, {"tactic", std::string(kContinuationTacticVersion)}}},
            {"completeness", O{{"capture", s.event.complete_capture ? "instruction_bytes_complete" : "instruction_bytes_unavailable_or_partial"},
                {"entry_context", (s.before.flags && s.before.b && s.before.hl && s.before.ix && s.before.iy)
                    ? "transfer_inputs_captured" : "partial_or_absent"},
                {"target_provenance", target_basis},
                {"continuation", continuation.status},
                {"scope", "one_observation"}, {"possible_additional_usages", true},
                {"unresolved", std::move(unresolved)}}}});
        grouped[{s.event.start, s.event.bytes, f.mechanism, s.event.next_pc,
                 f.taken ? int(*f.taken) : -1}].emplace_back(s.id);
    }
    for (const auto& [key, samples] : grouped) {
        const auto& [start, bytes, mechanism, target, taken] = key;
        A encoded;
        for (auto b : bytes) encoded.emplace_back(int(b));
        edges.emplace_back(O{{"start", int(start)}, {"bytes", std::move(encoded)}, {"next_pc", int(target)},
            {"mechanism", mechanism_name(mechanism)}, {"taken", taken < 0 ? J{} : J(bool(taken))},
            {"count", static_cast<uint32_t>(samples.size())}, {"samples", samples}});
    }
    return json::Write(O{{"format", "z80-transfer-report"}, {"version", 3},
        {"tactic", std::string(kTransferTacticVersion)}, {"capture_sha256", Sha256(json::Write(capture_json(capture)))},
        {"source", capture.source}, {"limitations", capture.limitations}, {"destination_sets_closed", false},
        {"occurrences", std::move(occurrences)}, {"edges", std::move(edges)}, {"value_graph", ValueGraphJson(values)}});
}
} // namespace z80::dbg::analysis
