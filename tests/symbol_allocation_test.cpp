// Verify failed allocations cannot split SymbolTable's two indexes.
// Copyright (c) 2026 Larry Dawson. Licensed under the MIT License.
#include "analysis_workspace.h"
#include "symbol_table.h"

#include <cstdlib>
#include <iostream>
#include <new>
#include <string>

namespace {
// Only armed during the operation under test, never during setup or assertions.
int allocations_left = -1;
} // namespace

void *operator new(std::size_t size) {
    if (allocations_left == 0)
        throw std::bad_alloc{};
    if (allocations_left > 0)
        --allocations_left;
    if (void *memory = std::malloc(size ? size : 1))
        return memory;
    throw std::bad_alloc{};
}
void *operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void *memory) noexcept { std::free(memory); }
void operator delete[](void *memory) noexcept { std::free(memory); }
void operator delete(void *memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void *memory, std::size_t) noexcept { std::free(memory); }

int main() {
    using namespace z80::dbg;
    const std::string old_name(100, 'A'), new_name(100, 'B');
    int failures = 0;
    for (bool replace : {false, true}) {
        bool reached_success = false;
        int failures_injected = 0;
        for (int budget = 0; budget < 64; ++budget) {
            SymbolTable table;
            table.Define({0x4000, old_name, SymbolType::DataRegion, "original region", 128});
            const Symbol candidate{static_cast<uint16_t>(replace ? 0x4000 : 0x8000), new_name,
                                   SymbolType::Function, std::string(200, 'D'), 16};
            bool threw = false;
            allocations_left = budget;
            try {
                table.Define(candidate);
            } catch (const std::bad_alloc &) {
                threw = true;
            }
            allocations_left = -1;
            if (threw) {
                ++failures_injected;
                const auto old = table.Lookup(0x4000);
                if (table.Size() != 1 || !old || old->name != old_name ||
                    old->description != "original region" || old->size != 128 ||
                    old->type != SymbolType::DataRegion || table.Resolve(old_name) != 0x4000 ||
                    table.Resolve(new_name) || table.Lookup(0x8000))
                    ++failures;
                // A later normal operation must still work after rollback.
                table.Rename(0x4000, "AFTER_FAILURE");
                if (table.Resolve(old_name) || table.Resolve("AFTER_FAILURE") != 0x4000)
                    ++failures;
            } else {
                reached_success = true;
                if (table.Resolve(new_name) != candidate.address || table.Size() != (replace ? 1u : 2u) ||
                    (replace && table.Resolve(old_name)))
                    ++failures;
                break;
            }
        }
        if (!reached_success || failures_injected == 0)
            ++failures;
    }
    // Failure after editing a staged project (including rebuilding its view or
    // hashing its revision) must not publish half of a workspace transaction.
    bool completed_workspace_edit = false;
    for (int budget = 0; budget < 1024; ++budget) {
        analysis::Workspace workspace;
        std::vector<uint8_t> image(16);
        if (!workspace.Initialize(image, 0x8000))
            return 1;
        auto id = workspace.Apply([&](analysis::Project &p) { return p.Create(p.Bind(0x8000), old_name); });
        if (!id)
            return 1;
        const auto before = analysis::Serialize(*workspace.Active());
        const auto revision = workspace.CurrentRevision();
        bool threw = false;
        allocations_left = budget;
        try {
            auto result = workspace.Apply([&](analysis::Project &p) {
                return p.Edit(*id, {{analysis::Field::Name, new_name},
                                    {analysis::Field::Summary, std::string(200, 'D')}});
            });
            if (!result)
                ++failures;
        } catch (const std::bad_alloc &) {
            threw = true;
        }
        allocations_left = -1;
        if (threw) {
            if (analysis::Serialize(*workspace.Active()) != before ||
                workspace.CurrentRevision() != revision || workspace.Symbols().Resolve(old_name) != 0x8000 ||
                workspace.Symbols().Resolve(new_name))
                ++failures;
        } else {
            completed_workspace_edit = true;
            if (workspace.Symbols().Resolve(old_name) != 0x8000 ||
                workspace.Symbols().Resolve(new_name) != 0x8000 || workspace.CurrentRevision() == revision)
                ++failures;
            break;
        }
    }
    if (!completed_workspace_edit)
        ++failures;
    if (failures)
        std::cerr << failures << " allocation rollback failures\n";
    return failures ? 1 : 0;
}
