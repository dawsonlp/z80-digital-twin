//
// Z80 Digital Twin - beeper resampler verification
// Copyright (c) 2025 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//
// Verifies the 1-bit beeper -> PCM resampler deterministically: a constant level
// yields a constant DC sample, and a square-wave edge stream yields an
// oscillating signal at roughly the right rate (sample count and zero crossings).
//

#include "spectrum/beeper.h"

#include <cstdint>
#include <iostream>
#include <vector>

namespace {

using z80::machine::spectrum::BeeperResampler;

constexpr uint32_t kCpuHz = 3'500'000;
constexpr uint32_t kRate  = 44100;

int failures = 0;
void check(bool ok, const char* what) {
    std::cout << (ok ? "  ✓ " : "  ✗ ") << what << '\n';
    if (!ok) ++failures;
}

} // namespace

int main() {
    std::cout << "Beeper resampler verification\n=============================\n";

    std::cout << "\n[1] Sample rate: ~one second -> ~sample_rate samples\n";
    {
        BeeperResampler r(kCpuHz, kRate);
        std::vector<int16_t> out;
        r.advance(kCpuHz, out);   // 1 second of T-cycles
        check(out.size() >= kRate - 2 && out.size() <= kRate + 2,
              "≈44100 samples for one second");
    }

    std::cout << "\n[2] Constant level -> constant DC sample\n";
    {
        BeeperResampler r(kCpuHz, kRate);
        std::vector<int16_t> out;
        r.edge(0, 1, out);            // speaker high from t=0
        r.advance(kCpuHz / 100, out); // 10 ms
        bool all_high = !out.empty();
        for (int16_t s : out) if (s < 8000) all_high = false;   // ~ +amp
        check(all_high, "level high -> samples near +amplitude");

        BeeperResampler r0(kCpuHz, kRate);
        std::vector<int16_t> out0;
        r0.advance(kCpuHz / 100, out0);   // level 0 default
        bool all_low = !out0.empty();
        for (int16_t s : out0) if (s > -8000) all_low = false;  // ~ -amp
        check(all_low, "level low -> samples near -amplitude");
    }

    std::cout << "\n[3] 1 kHz square wave -> oscillating signal\n";
    {
        BeeperResampler r(kCpuHz, kRate);
        std::vector<int16_t> out;
        // 1 kHz: period 3500 T, half-period 1750 T. Toggle for ~0.1 s.
        const uint64_t half = kCpuHz / 2000;   // 1750
        int level = 0;
        uint64_t t = 0;
        for (int i = 0; i < 200; ++i) {        // 100 full periods
            level ^= 1;
            r.edge(t, level, out);
            t += half;
        }
        r.advance(t, out);

        int16_t mn = 32767, mx = -32768;
        long sum = 0;
        int crossings = 0;
        int16_t prev = 0;
        for (int16_t s : out) {
            mn = std::min(mn, s); mx = std::max(mx, s); sum += s;
            if ((prev < 0 && s > 0) || (prev > 0 && s < 0)) ++crossings;
            prev = s;
        }
        check(mn < -4000 && mx > 4000, "signal swings both ways");
        check(out.size() > 4000, "produced ~0.1 s of samples");
        // ~100 periods -> ~200 zero crossings (allow generous tolerance).
        check(crossings > 120 && crossings < 280, "~1 kHz oscillation in the output");
        check(sum > -static_cast<long>(out.size()) * 2000 &&
              sum < static_cast<long>(out.size()) * 2000, "roughly zero-mean (centred)");
    }

    std::cout << "\n=============================\n";
    if (failures == 0) {
        std::cout << "✅ ALL BEEPER CHECKS PASSED\n";
        return 0;
    }
    std::cout << "❌ " << failures << " check(s) FAILED\n";
    return 1;
}
