//
// Z80 Digital Twin - ZX Spectrum boot verification (headless)
// Copyright (c) 2025-2026 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//
// Boots the 48K ROM through the SpectrumMachine for a couple of seconds of
// frames and checks the rendered picture actually drew something — the ROM's
// power-on screen (the copyright line at the bottom). The ROM is not in the repo
// (copyright); if it can't be found this test SKIPS (passes with a notice) so a
// clean checkout still goes green.
//
// Set Z80_SPEC48_ROM to point at the image, or run from the repo root.
//

#include "spectrum/spectrum_machine.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

namespace sm = z80::machine::spectrum;

int failures = 0;
void check(bool ok, const char* what) {
    std::cout << (ok ? "  ✓ " : "  ✗ ") << what << '\n';
    if (!ok) ++failures;
}

std::vector<uint8_t> read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)),
                                std::istreambuf_iterator<char>());
}

std::vector<uint8_t> find_rom() {
    std::vector<std::string> paths;
    if (const char* env = std::getenv("Z80_SPEC48_ROM")) paths.emplace_back(env);
    paths.insert(paths.end(), {"spec48.rom", "../spec48.rom", "../../spec48.rom"});
    for (const auto& p : paths) {
        auto data = read_file(p);
        if (!data.empty()) {
            std::cout << "  (using ROM: " << p << ", " << data.size() << " bytes)\n";
            return data;
        }
    }
    return {};
}

} // namespace

int main() {
    std::cout << "Spectrum boot verification\n==========================\n";

    const std::vector<uint8_t> rom = find_rom();
    if (rom.empty()) {
        std::cout << "  SKIP: spec48.rom not found (set Z80_SPEC48_ROM or run from repo root)\n";
        return 0;
    }

    sm::SpectrumMachine machine;
    check(machine.load_rom(rom), "ROM loaded");

    constexpr int kBootFrames = 200;   // ~4 s of emulated time
    for (int i = 0; i < kBootFrames; ++i) machine.run_frame();
    check(machine.frame_count() == kBootFrames, "ran the boot frames");

    std::array<uint8_t, sm::SpectrumMachine::kPixels> frame{};
    machine.render_indices(frame);

    // Histogram of palette indices over the whole frame.
    std::array<int, 16> hist{};
    for (uint8_t px : frame) ++hist[px & 0x0F];
    int distinct = 0;
    for (int c : hist) if (c > 0) ++distinct;

    std::cout << "  border colour = " << static_cast<int>(machine.ula().border())
              << ", distinct palette indices = " << distinct << "\n  histogram:";
    for (int i = 0; i < 16; ++i) if (hist[i]) std::cout << " " << i << ":" << hist[i];
    std::cout << "\n";

    check(distinct >= 2, "screen is not a single flat colour (something rendered)");

    // The copyright line sits in the lower display region: look for ink/paper
    // variation there (text), proving the ROM drew to the screen, not just border.
    bool lower_text = false;
    const int x0 = sm::video::kBorderLeft;
    const int x1 = x0 + sm::video::kDisplayWidth;
    const int y0 = sm::video::kBorderTop + sm::video::kDisplayHeight - 40;  // last ~5 char rows
    const int y1 = sm::video::kBorderTop + sm::video::kDisplayHeight;
    uint8_t first = frame[static_cast<std::size_t>(y0) * sm::video::kFrameWidth + x0];
    for (int y = y0; y < y1 && !lower_text; ++y)
        for (int x = x0; x < x1; ++x)
            if (frame[static_cast<std::size_t>(y) * sm::video::kFrameWidth + x] != first) {
                lower_text = true;
                break;
            }
    check(lower_text, "lower display region has content (copyright text)");

    std::cout << "\n==========================\n";
    if (failures == 0) {
        std::cout << "✅ SPECTRUM BOOTED AND RENDERED\n";
        return 0;
    }
    std::cout << "❌ " << failures << " check(s) FAILED\n";
    return 1;
}
