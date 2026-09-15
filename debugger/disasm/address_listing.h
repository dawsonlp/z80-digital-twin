#ifndef Z80_DBG_ADDRESS_LISTING_H
#define Z80_DBG_ADDRESS_LISTING_H

#include "instruction_history.h"
#include "disassembler.h"
#include <algorithm>
#include <vector>

namespace z80::dbg {

struct AddressRow {
    uint16_t address;
    uint32_t available;
    bool raw;     // incomplete speculative decoding at an anchor/address boundary
    bool overlap; // another observed start occurs inside this observed span
};

// Address order is a view, never execution order. Unknown regions are tentative
// decoding. Every retained unchanged start, PC and requested destination is a row.
inline std::vector<AddressRow> BuildAddressListing(const MetadataMemory& memory,
        const InstructionHistory& history, const Disassembler& decoder,
        uint16_t pc, uint16_t destination) {
    // Modified starts are inspection markers only; their new decoding remains tentative.
    auto anchors = history.RetainedStarts();
    anchors.push_back(pc); anchors.push_back(destination);
    std::sort(anchors.begin(), anchors.end());
    anchors.erase(std::unique(anchors.begin(), anchors.end()), anchors.end());
    const ByteReader read = [&](uint16_t a) { return memory[a]; };
    std::vector<AddressRow> rows;
    std::size_t anchor_index = 0;
    for (uint32_t address = 0; address < 65536;) {
        while (anchor_index < anchors.size() && anchors[anchor_index] <= address) ++anchor_index;
        const uint32_t next_anchor = anchor_index < anchors.size() ? anchors[anchor_index] : 65536;
        const auto* event = history.Latest(uint16_t(address));
        const bool observed = event && history.State(*event, memory) == EvidenceState::Observed;
        const uint32_t available = observed ? uint32_t(event->bytes.size()) : std::min(next_anchor - address, uint32_t{256});
        const auto ins = decoder.Decode(read, uint16_t(address), {}, available);
        const bool raw = !ins.complete;
        // Do not swallow a reliable/selected boundary behind a guessed decode.
        const uint32_t length = raw ? std::min(available, uint32_t{4}) : std::max(ins.length, uint32_t{1});
        rows.push_back({uint16_t(address), raw ? length : available, raw,
                        observed && address + length > next_anchor && next_anchor < 65536});
        address = std::min(address + length, next_anchor);
    }
    return rows;
}

} // namespace z80::dbg
#endif
