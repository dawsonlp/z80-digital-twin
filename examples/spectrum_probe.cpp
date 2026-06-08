//
// Z80 Digital Twin - headless ZX Spectrum instrumentation probe
// Copyright (c) 2025-2026 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//
// A no-GUI way to *drive and observe* a running Spectrum from the command line:
// boot a ROM, type on the keyboard matrix (including the LOAD keyword + quotes),
// play a tape, and watch what the CPU does — code coverage, RAM writes, the PC
// hot-spot, the border, and an ASCII view of the screen. It exists to explore
// loader/timing/I-O behaviour (e.g. "does Underwurlde load past its turbo
// hand-off, or freeze in a tight loop?") without a display.
//
// How it works (the three layers this repo is built from):
//   * SpectrumMachine wires the templated CPU to the ULA, tape, and frame clock
//     (machine/spectrum/spectrum_machine.h) — and is deliberately headless.
//   * DebugSession (debugger/exec) drives that same CPU one instruction at a time
//     with full instrumentation: per-address execution coverage, dirty-RAM
//     tracking, and self-modifying-code detection. We run each PAL frame through
//     the session instead of the machine's raw stepper, so every frame's activity
//     is measured.
//   * The keyboard is the ULA's 8x5 matrix; "typing" is just holding the right
//     row/bit low for a few frames so the ROM's interrupt-driven key scan sees it.
//
// Usage:
//   spectrum_probe [rom.rom] [--tape FILE] [--load] [--type "KEYS"]
//                  [--boot N] [--frames N] [--window N] [--screen] [-h]
// See --help for the full list. With no ROM path it looks for $Z80_SPEC48_ROM,
// then ./spec48.rom, ../spec48.rom.
//

#include "spectrum/spectrum_machine.h"
#include "spectrum/keyboard.h"
#include "spectrum/screen.h"
#include "spectrum/video.h"
#include "spectrum/timing.h"
#include "debug_session.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

namespace {

namespace sm = z80::machine::spectrum;
namespace kb = z80::machine::spectrum::keyboard;
using z80::dbg::DebugSession;

std::vector<uint8_t> read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)),
                                std::istreambuf_iterator<char>());
}

std::vector<uint8_t> find_rom(const std::string& explicit_path) {
    std::vector<std::string> paths;
    if (!explicit_path.empty()) paths.push_back(explicit_path);
    if (const char* env = std::getenv("Z80_SPEC48_ROM")) paths.emplace_back(env);
    paths.insert(paths.end(), {"spec48.rom", "../spec48.rom", "../../spec48.rom"});
    for (const auto& p : paths) {
        auto data = read_file(p);
        if (!data.empty()) { std::cout << "ROM: " << p << " (" << data.size() << " bytes)\n"; return data; }
    }
    return {};
}

// -- The instrumented frame ------------------------------------------------
//
// Mirrors SpectrumMachine::run_frame(), but advances the CPU through the
// DebugSession (the breakpoint-aware, coverage-tracking stepper) instead of the
// machine's raw inner loop. begin_frame()/end_frame() keep the ULA's border
// timeline and frame counter correct; Interrupt(0xFF) asserts the 50 Hz /INT
// (and wakes the ROM from its idle HALT). The session measures everything that
// happens in between.
void run_instrumented_frame(sm::SpectrumMachine& machine, DebugSession& session) {
    machine.ula().begin_frame();
    machine.cpu().Interrupt(0xFF);       // frame interrupt; wakes HALT if IFF1 set
    session.Run();
    session.RunForTStates(sm::timing::kTPerFrame);
    machine.ula().end_frame();
}

// -- Keyboard injection (the 8x5 matrix) -----------------------------------
//
// A "chord" is the set of matrix keys held down together (e.g. SYMBOL SHIFT +
// P = the " character). The real keyboard is a level the ROM polls each frame,
// so to register a keystroke we hold the rows low for a few frames, then release
// for a few — exactly what a human key press/release looks like to the scan.
struct Chord {
    std::vector<kb::Key> keys;
    const char* note;   // for logging
};

void press_chord(sm::SpectrumMachine& machine, DebugSession& session, const Chord& chord,
                 int hold_frames, int gap_frames) {
    machine.ula().release_all_keys();
    for (const kb::Key& k : chord.keys) machine.ula().key_down(k.half_row, k.bit);
    for (int i = 0; i < hold_frames; ++i) run_instrumented_frame(machine, session);
    machine.ula().release_all_keys();
    for (int i = 0; i < gap_frames; ++i) run_instrumented_frame(machine, session);
}

// Translate a key-script string into chords. Most characters map to their letter
// or digit key; a few tokens cover the cases that matter for loading a game:
//   L  -> the J key, which at the BASIC keyword cursor types the LOAD token
//   "  -> SYMBOL SHIFT + P
//   _  -> SPACE          \n / ENTER -> ENTER
// Anything else falls back to its ASCII matrix key (uppercased).
std::vector<Chord> script_to_chords(const std::string& in) {
    // Honour a literal "\n" (backslash-n) from the shell as ENTER, so
    // --type "P\n" works without the shell having to emit a real newline.
    std::string s;
    for (std::size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '\\' && i + 1 < in.size() && in[i + 1] == 'n') { s.push_back('\n'); ++i; }
        else s.push_back(in[i]);
    }
    std::vector<Chord> out;
    for (char c : s) {
        switch (c) {
        case 'L': out.push_back({{kb::key_for_ascii('J')}, "LOAD keyword (J key)"}); break;
        case '"': out.push_back({{kb::kSymbolShift, kb::key_for_ascii('P')}, "\" (SYM+P)"}); break;
        case '_': case ' ': out.push_back({{kb::kSpace}, "SPACE"}); break;
        case '\n': out.push_back({{kb::kEnter}, "ENTER"}); break;
        default: {
            const kb::Key k = kb::key_for_ascii(static_cast<char>(std::toupper(c)));
            if (k.valid()) out.push_back({{k}, "key"});
            break;
        }
        }
    }
    return out;
}

void type_script(sm::SpectrumMachine& machine, DebugSession& session, const std::string& script) {
    std::cout << "Typing on the keyboard matrix: \"" << script << "\"\n";
    for (const Chord& chord : script_to_chords(script))
        press_chord(machine, session, chord, /*hold=*/4, /*gap=*/4);
}

// -- Screen as ASCII --------------------------------------------------------
//
// Render the frame to palette indices, take the most common index as "paper",
// and print the 256x192 display area downsampled to one char per 4x8 cell:
// '#' if the cell holds any non-paper (ink) pixel, ' ' otherwise. Enough to read
// text and make out sprites headlessly.
void dump_screen_ascii(const sm::SpectrumMachine& machine) {
    std::array<uint8_t, sm::SpectrumMachine::kPixels> px{};
    machine.render_indices(px);

    std::array<int, 16> hist{};
    for (uint8_t p : px) ++hist[p & 0x0F];
    const uint8_t paper = static_cast<uint8_t>(
        std::distance(hist.begin(), std::max_element(hist.begin(), hist.end())));

    const int x0 = sm::video::kBorderLeft, y0 = sm::video::kBorderTop;
    std::cout << "Screen (" << (sm::video::kDisplayWidth / 4) << "x"
              << (sm::video::kDisplayHeight / 8) << " of 256x192, '#'=ink):\n";
    for (int cy = 0; cy < sm::video::kDisplayHeight; cy += 8) {
        std::cout << "  |";
        for (int cx = 0; cx < sm::video::kDisplayWidth; cx += 4) {
            bool ink = false;
            for (int dy = 0; dy < 8 && !ink; ++dy)
                for (int dx = 0; dx < 4; ++dx) {
                    const std::size_t i = static_cast<std::size_t>(y0 + cy + dy) *
                                              sm::video::kFrameWidth + (x0 + cx + dx);
                    if ((px[i] & 0x0F) != paper) { ink = true; break; }
                }
            std::cout << (ink ? '#' : ' ');
        }
        std::cout << "|\n";
    }
}

// -- Activity report over a window of frames -------------------------------
//
// The signature that distinguishes "loading / running" from "frozen in a tight
// loop": a working loader keeps executing fresh code (coverage grows) and keeps
// writing loaded bytes into RAM (dirty RAM outside the screen file grows). A
// freeze executes the same handful of bytes forever — coverage and RAM writes
// flatline while the PC stays pinned to a tiny address range.
struct Window {
    uint32_t cov_before = 0;
    uint16_t pc_min = 0xFFFF, pc_max = 0;
    std::map<uint16_t, int> pc_pages;   // PC>>8 -> sample count (frame-boundary)
    int non_screen_writes = 0;
};

void report_window(DebugSession& session, sm::SpectrumMachine& machine, int frames, int window) {
    std::cout << "\nframe   +code   RAMwr  PC-range        hotpage  border  state\n";
    Window w;
    w.cov_before = session.CoveredBytes();
    session.ClearDirty();

    for (int f = 1; f <= frames; ++f) {
        run_instrumented_frame(machine, session);

        const uint16_t pc = machine.cpu().PC();
        w.pc_min = std::min(w.pc_min, pc);
        w.pc_max = std::max(w.pc_max, pc);
        ++w.pc_pages[static_cast<uint16_t>(pc >> 8)];

        if (f % window == 0 || f == frames) {
            // Count RAM writes outside the display file (0x4000-0x5AFF): real load
            // progress, not just the screen being painted.
            int ram = 0;
            for (uint16_t a : session.DirtyAddresses())
                if (a < 0x4000 || a > 0x5AFF) ++ram;
            w.non_screen_writes = ram;

            const uint32_t new_code = session.CoveredBytes() - w.cov_before;
            uint16_t hot = 0; int hot_n = -1;
            for (auto [page, n] : w.pc_pages) if (n > hot_n) { hot_n = n; hot = page; }
            const bool in_rom = w.pc_max < 0x4000;
            const bool tight = (w.pc_max - w.pc_min) < 0x100;

            // Frozen/idle == executing no fresh code, writing essentially no RAM
            // (a spin may still poke a counter), pinned to a tiny PC range. A real
            // loader fails the RAMwr test (thousands of writes/window); the BASIC
            // editor idles the same way but in ROM, so distinguish the two.
            const char* state = "running";
            if (new_code == 0 && ram < 16 && tight)
                state = in_rom ? "IDLE/spin (ROM)" : "FROZEN (tight loop in RAM)";

            std::printf("%5d  %6u  %6d  %04X-%04X (%4u)  %02X00     %d       %s\n",
                        f, new_code, ram, w.pc_min, w.pc_max,
                        static_cast<unsigned>(w.pc_max - w.pc_min), hot,
                        machine.ula().border(), state);

            // reset the window
            w.cov_before = session.CoveredBytes();
            w.pc_min = 0xFFFF; w.pc_max = 0; w.pc_pages.clear();
            session.ClearDirty();
        }
    }
}

void print_usage(const char* prog) {
    std::cout <<
        "Headless ZX Spectrum instrumentation probe — Z80 Digital Twin\n\n"
        "Boots a 48K ROM, optionally types on the keyboard matrix and plays a tape,\n"
        "and reports per-window CPU activity (code coverage, RAM writes, PC hot-spot,\n"
        "border) with a freeze detector. No display required.\n\n"
        "Usage:\n  " << prog << " [rom.rom] [options]\n\n"
        "Options:\n"
        "  --tape FILE     Load a .tap/.tzx image (auto-detected).\n"
        "  --load          Type LOAD\"\" + ENTER, then play the tape (implies a tape).\n"
        "  --type \"KEYS\"   Type a key-script before running. Specials: L=LOAD token,\n"
        "                  \"=SYM+P quote, _ or space=SPACE, newline=ENTER.\n"
        "  --play          Start the tape (without typing LOAD).\n"
        "  --boot N        Frames to boot before typing (default 100).\n"
        "  --frames N      Frames to run and instrument after load (default 2500).\n"
        "  --window N      Report every N frames (default 100).\n"
        "  --screen        Dump the screen as ASCII at the end.\n"
        "  -h, --help      Show this help.\n\n"
        "Examples:\n"
        "  " << prog << " spec48.rom --tape underwurlde.tzx --load --screen\n"
        "  " << prog << " spec48.rom --boot 200 --screen        # just boot to BASIC\n";
}

} // namespace

int main(int argc, char** argv) {
    std::string rom_path, tape_path, type_script_str;
    int boot = 100, frames = 2500, window = 100;
    bool do_load = false, do_play = false, do_screen = false;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "-h" || a == "--help") { print_usage(argv[0]); return 0; }
        else if (a == "--tape" && i + 1 < argc) tape_path = argv[++i];
        else if (a == "--type" && i + 1 < argc) type_script_str = argv[++i];
        else if (a == "--load") do_load = true;
        else if (a == "--play") do_play = true;
        else if (a == "--screen") do_screen = true;
        else if (a == "--boot" && i + 1 < argc) boot = std::atoi(argv[++i]);
        else if (a == "--frames" && i + 1 < argc) frames = std::atoi(argv[++i]);
        else if (a == "--window" && i + 1 < argc) window = std::atoi(argv[++i]);
        else if (!a.empty() && a[0] != '-') rom_path = a;
        else std::cerr << "Unknown argument: " << a << "\n";
    }
    if (window < 1) window = 1;

    const std::vector<uint8_t> rom = find_rom(rom_path);
    if (rom.empty()) { std::cerr << "No ROM found. Pass a path or set Z80_SPEC48_ROM.\n"; return 1; }

    sm::SpectrumMachine machine;
    if (!machine.load_rom(rom)) { std::cerr << "Failed to load ROM (<=16 KB).\n"; return 1; }
    machine.set_rom_write_protect(true);

    // The DebugSession drives the very CPU the machine runs (same template config),
    // giving full instrumentation over the live machine.
    DebugSession session(machine.cpu());

    if (!tape_path.empty()) {
        const std::vector<uint8_t> tape = read_file(tape_path);
        if (tape.empty() || !machine.load_tape(tape)) { std::cerr << "Failed to load tape.\n"; return 1; }
        std::cout << "Tape: " << tape_path << " (" << machine.tape().block_count()
                  << " blocks, " << machine.tape().pulse_count() << " pulses, "
                  << machine.tape().total_tstates() / sm::timing::kCpuHz << "s)\n";
    }

    std::cout << "Booting " << boot << " frames to BASIC...\n";
    for (int i = 0; i < boot; ++i) run_instrumented_frame(machine, session);

    if (do_load) type_script(machine, session, "L\"\"\n");
    else if (!type_script_str.empty()) type_script(machine, session, type_script_str);

    if (do_load || do_play) {
        machine.play_tape();
        std::cout << "Tape: play (cycle " << machine.cpu().GetCycleCount() << ")\n";
    }

    std::cout << "\nInstrumenting " << frames << " frames (~" << frames / 50 << "s emulated):";
    report_window(session, machine, frames, window);

    std::cout << "\nTotals: coverage " << session.CoveredBytes() << " bytes ("
              << session.CoveragePercent() << "%), SMC writes " << session.SmcCount()
              << ", frame " << machine.frame_count() << ", PC=" << std::hex << machine.cpu().PC()
              << std::dec << "\n";

    if (do_screen) { std::cout << "\n"; dump_screen_ascii(machine); }
    return 0;
}
