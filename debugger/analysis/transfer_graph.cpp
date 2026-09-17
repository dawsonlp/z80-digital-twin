// Copyright (c) 2026 Larry Dawson. Licensed under the MIT License (see LICENSE).
#include "transfer_graph.h"
#include <array>
#include <format>
#include <map>
#include <set>
#include <tuple>

namespace z80::dbg::analysis {
namespace {
using J = json::Value;
using A = J::Array;
using O = J::Object;
using Site = std::pair<uint16_t, std::vector<uint8_t>>;
template<class T> A array(const T& values) {
    A result;
    for (const auto& value : values) result.emplace_back(value);
    return result;
}
std::string mechanism(TransferMechanism m) {
    constexpr std::array names = {"unknown", "sequential", "jump", "indirect_jump", "call",
        "restart", "return_instruction", "interrupt_return_instruction", "repeat", "halt", "machine_transition"};
    return names.at(size_t(m));
}
bool inconsistent(const TransferFinding& f) {
    // These are classifier contradictions, not missing target ancestry or stack
    // interpretation. Neither an absent value graph nor an unmatched RET makes
    // an otherwise observed architectural call uncertain.
    for (const auto& reason : f.unresolved)
        if (reason == "observed PC disagrees with decoded effect and entry context" ||
            reason == "observed PC disagrees with entry target register" ||
            reason == "repeat successor is inconsistent with instruction") return true;
    return false;
}
std::string category(const TransferFinding& f) {
    if (inconsistent(f) || f.mechanism == TransferMechanism::Unknown) return "unresolved";
    if (f.mechanism == TransferMechanism::MachineTransition) return "machine_transition";
    if (f.mechanism == TransferMechanism::Sequential) return "fallthrough";
    if (f.mechanism == TransferMechanism::Halt) return "halt";
    if (!f.taken) return "unresolved";
    if (!*f.taken) return "fallthrough";
    return mechanism(f.mechanism);
}
bool conditional(const TransferSample& sample, const TransferFinding& f) {
    if (f.mechanism == TransferMechanism::Unknown || f.mechanism == TransferMechanism::MachineTransition) return false;
    size_t i = 0;
    while (i < sample.event.bytes.size() && (sample.event.bytes[i] == 0xDD || sample.event.bytes[i] == 0xFD)) ++i;
    if (i == sample.event.bytes.size()) return false;
    const auto op = sample.event.bytes[i];
    return (op & 0xC7) == 0xC4 || (op & 0xC7) == 0xC0 || (op & 0xC7) == 0xC2 ||
        (op & 0xE7) == 0x20 || op == 0x10;
}
struct Metric {
    std::set<uint32_t> sites;
    std::set<int> addresses, destinations;
    std::vector<std::string> samples;
    void add(uint32_t site, const TransferSample& s) {
        sites.insert(site); addresses.insert(s.event.start); destinations.insert(s.event.next_pc); samples.push_back(s.id);
    }
    J json() const {
        return O{{"site_variant_count", uint32_t(sites.size())}, {"source_address_count", uint32_t(addresses.size())},
            {"occurrence_count", uint32_t(samples.size())}, {"destination_count", uint32_t(destinations.size())},
            {"source_sites", array(sites)}, {"source_addresses", array(addresses)},
            {"destinations", array(destinations)}, {"samples", array(samples)}};
    }
};
struct Vertex {
    Metric all;
    std::map<std::string, Metric> categories;
    std::map<std::string, std::vector<std::string>> outcomes;
};
struct Target {
    Metric calls;
    std::map<std::string, Metric> categories;
    std::map<std::string, Metric> relationships;
    std::vector<uint32_t> edges;
};
struct Edge {
    std::vector<std::string> samples;
    std::optional<uint16_t> encoded_target;
    std::map<std::string, std::vector<std::string>> usages;
};
template<class T> O metrics(const T& groups) {
    O out;
    for (const auto& [name, metric] : groups) out.emplace(name, metric.json());
    return out;
}
}
J TransferGraphJson(const TransferCapture& capture, const std::vector<TransferFinding>& transfers,
                    const std::vector<OccurrenceResolution>& resolutions) {
    std::map<Site, uint32_t> ids;
    for (const auto& s : capture.samples) ids.emplace(Site{s.event.start, s.event.bytes}, 0);
    uint32_t next = 0;
    std::map<int, std::vector<uint32_t>> variants;
    for (auto& [site, id] : ids) { id = ++next; variants[site.first].push_back(id); }
    std::map<uint32_t, Vertex> sources;
    std::map<int, Target> targets;
    // Keep architectural mechanism/outcome even when the effective category is
    // unresolved or fallthrough. A later usage finding does not split an event.
    using Key = std::tuple<uint32_t, int, TransferMechanism, int, std::string>;
    std::map<Key, Edge> edges;
    for (size_t i = 0; i < capture.samples.size(); ++i) {
        const auto& s = capture.samples[i]; const auto& f = transfers.at(i);
        const auto id = ids.at({s.event.start, s.event.bytes}); const auto kind = category(f);
        auto& source = sources[id]; auto& target = targets[s.event.next_pc];
        source.all.add(id, s); source.categories[kind].add(id, s); target.categories[kind].add(id, s);
        target.relationships[resolutions.at(i).continuation_relationship].add(id, s);
        if (kind == "call" || kind == "restart") target.calls.add(id, s);
        if (conditional(s, f))
            source.outcomes[inconsistent(f) || !f.taken ? "unresolved" : *f.taken ? "taken" : "untaken"].push_back(s.id);
        auto& edge = edges[{id, s.event.next_pc, f.mechanism, f.taken ? int(*f.taken) : -1, kind}];
        edge.samples.push_back(s.id);
        edge.encoded_target = f.encoded_target;
        edge.usages[resolutions.at(i).continuation_relationship].push_back(s.id);
    }
    A edge_json; next = 0;
    for (const auto& [key, edge] : edges) {
        const auto& [source, target, m, taken, kind] = key;
        const auto id = ++next; targets[target].edges.push_back(id);
        A usages;
        for (const auto& [name, samples] : edge.usages)
            usages.emplace_back(O{{"relationship", name}, {"samples", array(samples)}});
        edge_json.emplace_back(O{{"id", id}, {"source_site", source}, {"target_address", target},
            {"category", kind}, {"mechanism", mechanism(m)}, {"taken", taken < 0 ? J{} : J(bool(taken))},
            {"encoded_target", edge.encoded_target ? J(int(*edge.encoded_target)) : J{}},
            {"occurrence_count", uint32_t(edge.samples.size())}, {"samples", array(edge.samples)}, {"usage_variants", std::move(usages)}});
    }
    A site_json;
    for (const auto& [site, id] : ids) {
        const auto& source = sources.at(id); O outcomes;
        for (const auto name : {"taken", "untaken", "unresolved"}) {
            const auto found = source.outcomes.find(name);
            const auto samples = found == source.outcomes.end() ? std::vector<std::string>{} : found->second;
            outcomes.emplace(name, O{{"count", uint32_t(samples.size())}, {"samples", array(samples)}});
        }
        site_json.emplace_back(O{{"id", id}, {"start", int(site.first)}, {"bytes", array(site.second)},
            {"successors", source.all.json()}, {"by_category", metrics(source.categories)}, {"conditional_outcomes", std::move(outcomes)}});
    }
    A target_json;
    for (const auto& [address, target] : targets) {
        const auto found = variants.find(address);
        // Same-address source variants are candidates, not evidence that a
        // particular target encoding was executed after this transfer.
        const auto candidates = found == variants.end() ? std::vector<uint32_t>{} : found->second;
        target_json.emplace_back(O{{"address", address}, {"incoming_edges", array(target.edges)},
            {"captured_site_candidates", array(candidates)}, {"target_variant_established", false},
            {"callers", target.calls.json()}, {"by_category", metrics(target.categories)},
            {"by_continuation_relationship", metrics(target.relationships)},
            {"comment", std::format("Observed {} taken CALL/RST source variants at {} addresses ({} occurrences) in this capture; additional callers remain possible.",
                target.calls.sites.size(), target.calls.addresses.size(), target.calls.samples.size())}});
    }
    return O{{"tactic", std::string(kTransferGraphVersion)}, {"scope", "one_capture"},
        {"sample_count", uint32_t(capture.samples.size())}, {"sample_limit", uint32_t(kMaxTransferSamples)},
        {"counts_complete_for_supplied_capture", true}, {"destination_sets_closed", false},
        {"routine_graph_status", "not_analyzed"},
        {"limitations", A{"Counts exclude execution outside this capture; no cross-capture event deduplication is implied.",
            "Sites are address plus captured bytes, scoped to this report; they are not durable run or image identities.",
            "Category and continuation-relationship metrics describe the same events; do not add their counts together.",
            "Zero observed calls does not establish no callers. Successors include fallthrough and returns, not just entries."}},
        {"sites", std::move(site_json)}, {"edges", std::move(edge_json)}, {"destinations", std::move(target_json)}};
}
std::string TransferGraphText(const J& graph) {
    std::string result = "Observed transfer graph — one capture; additional callers and targets remain possible.\n";
    result += std::format("{} samples; {} source variants; {} grouped edges. Routine graph not analyzed.\n",
        graph.at("sample_count").integer(), graph.at("sites").array().size(), graph.at("edges").array().size());
    for (const auto& target : graph.at("destinations").array()) {
        result += std::format("\n${:04X}: {}\n", target.at("address").integer(), target.at("comment").string());
        for (const auto& [kind, metric] : target.at("by_category").object())
            result += std::format("  {}: {} source variants, {} occurrences\n", kind,
                metric.at("site_variant_count").integer(), metric.at("occurrence_count").integer());
    }
    result += "\nEdges (site IDs and sample references resolve in the JSON report):\n";
    std::map<int64_t, int64_t> addresses;
    for (const auto& site : graph.at("sites").array()) addresses.emplace(site.at("id").integer(), site.at("start").integer());
    for (const auto& edge : graph.at("edges").array())
        result += std::format("  ${:04X} (site {}) -> ${:04X}: {} [{}], {} occurrences\n",
            addresses.at(edge.at("source_site").integer()), edge.at("source_site").integer(), edge.at("target_address").integer(), edge.at("category").string(),
            edge.at("mechanism").string(), edge.at("occurrence_count").integer());
    return result;
}
}
