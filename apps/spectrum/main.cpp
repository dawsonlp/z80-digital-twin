//
// Z80 Digital Twin - ZX Spectrum 48K viewer
// Copyright (c) 2025 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//
// Boots a 48K ROM on the SpectrumMachine and shows the running screen in a
// window (border + display, 3x). Headless mode (--shot FILE) renders N frames
// and writes a PPM — no display needed — for verification.
//
// Usage:
//   spectrum [rom.rom] [--frames N] [--shot FILE]
// With no path it looks for $Z80_SPEC48_ROM, then spec48.rom / ../spec48.rom.
//

#include "spectrum/spectrum_machine.h"
#include "spectrum/screen.h"
#include "spectrum/keyboard.h"
#include "spectrum/beeper.h"
#include "audio_output.h"

#define GL_SILENCE_DEPRECATION
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <fstream>
#include <iostream>
#include <iterator>
#include <span>
#include <string>
#include <vector>

namespace {

namespace sm = z80::machine::spectrum;

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
        if (!data.empty()) {
            std::cout << "ROM: " << p << " (" << data.size() << " bytes)\n";
            return data;
        }
    }
    return {};
}

int write_ppm(const std::string& path, const sm::SpectrumMachine& machine) {
    std::array<uint8_t, sm::SpectrumMachine::kPixels> idx{};
    machine.render_indices(idx);
    std::ofstream f(path, std::ios::binary);
    if (!f) { std::cerr << "cannot write " << path << "\n"; return 1; }
    f << "P6\n" << sm::video::kFrameWidth << " " << sm::video::kFrameHeight << "\n255\n";
    for (uint8_t px : idx) {
        const z80::machine::spectrum::screen::Rgb c = z80::machine::spectrum::screen::to_rgb(px);
        const char rgb[3] = {static_cast<char>(c.r), static_cast<char>(c.g), static_cast<char>(c.b)};
        f.write(rgb, 3);
    }
    std::cout << "shot: wrote " << sm::video::kFrameWidth << "x" << sm::video::kFrameHeight
              << " PPM to " << path << "\n";
    return 0;
}

void glfw_error_callback(int error, const char* description) {
    std::cerr << "GLFW error " << error << ": " << description << "\n";
}

// Translate the host keyboard's current state into the Spectrum matrix. GLFW
// key tokens for printable ASCII match uppercase ASCII (GLFW_KEY_A == 'A'), so
// the matrix's ascii table maps straight through. Level-polled each frame — the
// real keyboard is a level, not an edge, so this is exactly right.
void poll_keyboard(GLFWwindow* window, sm::Ula& ula) {
    namespace kb = z80::machine::spectrum::keyboard;
    ula.release_all_keys();

    const auto press = [&](kb::Key k) { ula.key_down(k.half_row, k.bit); };
    const auto down  = [&](int glfw_key) { return glfwGetKey(window, glfw_key) == GLFW_PRESS; };

    for (const kb::AsciiKey& k : kb::kAsciiKeys)
        if (down(k.c)) ula.key_down(k.half_row, k.bit);

    if (down(GLFW_KEY_ENTER)) press(kb::kEnter);
    if (down(GLFW_KEY_SPACE)) press(kb::kSpace);
    if (down(GLFW_KEY_LEFT_SHIFT) || down(GLFW_KEY_RIGHT_SHIFT)) press(kb::kCapsShift);
    if (down(GLFW_KEY_LEFT_CONTROL) || down(GLFW_KEY_RIGHT_CONTROL)) press(kb::kSymbolShift);
    // Backspace = DELETE = CAPS SHIFT + 0.
    if (down(GLFW_KEY_BACKSPACE)) { press(kb::kCapsShift); press(kb::key_for_ascii('0')); }
}

} // namespace

int main(int argc, char** argv) {
    std::string rom_path;
    std::string shot_path;
    std::string tape_path;
    int frames = 0;
    bool turbo = false;
    bool protect_rom = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--shot" && i + 1 < argc) shot_path = argv[++i];
        else if (arg == "--frames" && i + 1 < argc) frames = std::atoi(argv[++i]);
        else if (arg == "--tape" && i + 1 < argc) tape_path = argv[++i];
        else if (arg == "--turbo") turbo = true;
        else if (arg == "--protect-rom") protect_rom = true;
        else if (!arg.empty() && arg[0] != '-') rom_path = arg;
        else std::cerr << "Unknown argument: " << arg << "\n";
    }

    const std::vector<uint8_t> rom = find_rom(rom_path);
    if (rom.empty()) {
        std::cerr << "No ROM found. Pass a path or set Z80_SPEC48_ROM.\n";
        return 1;
    }

    sm::SpectrumMachine machine;
    if (!machine.load_rom(rom)) {
        std::cerr << "Failed to load ROM (size must be <= 16 KB).\n";
        return 1;
    }
    machine.set_rom_write_protect(protect_rom);
    if (protect_rom) std::cout << "ROM write-protected (0x0000-0x3FFF)\n";

    if (!tape_path.empty()) {
        const std::vector<uint8_t> tap = read_file(tape_path);
        if (tap.empty() || !machine.load_tape(tap)) {
            std::cerr << "Failed to load tape: " << tape_path << "\n";
        } else {
            std::cout << "Tape: " << tape_path << " (" << machine.tape().block_count()
                      << " blocks). Type LOAD\"\" then press F5 to play.\n";
        }
    }

    // -- Headless screenshot: no display needed ------------------------------
    if (!shot_path.empty()) {
        const int n = frames > 0 ? frames : 200;
        for (int i = 0; i < n; ++i) machine.run_frame();
        std::cout << "booted " << n << " frames; border colour = "
                  << static_cast<int>(machine.ula().border()) << "\n";
        return write_ppm(shot_path, machine);
    }

    // -- Live window ---------------------------------------------------------
    if (frames > 0) for (int i = 0; i < frames; ++i) machine.run_frame();

    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) { std::cerr << "Failed to init GLFW\n"; return 1; }

#if defined(__APPLE__)
    const char* glsl_version = "#version 150";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#else
    const char* glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
#endif

    constexpr int kScale = 3;
    GLFWwindow* window = glfwCreateWindow(sm::video::kFrameWidth * kScale,
                                          sm::video::kFrameHeight * kScale,
                                          "ZX Spectrum 48K — Z80 Digital Twin", nullptr, nullptr);
    if (!window) { std::cerr << "Failed to create window (no display?)\n"; glfwTerminate(); return 1; }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, sm::video::kFrameWidth, sm::video::kFrameHeight,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    std::array<uint32_t, sm::SpectrumMachine::kPixels> rgba{};

    // The Spectrum runs at 50.08 Hz; pace emulated frames to wall-clock with a
    // fixed timestep, independent of the display's vsync refresh. (vsync stays on
    // to avoid tearing; --turbo runs one frame per refresh instead.) The engine
    // does ~680x real-time headless, so this is purely a throttle.
    using clock = std::chrono::steady_clock;
    constexpr double kHz = z80::machine::spectrum::timing::kFrameRateHz;
    const double frame_period = 1.0 / kHz;
    auto last = clock::now();
    auto fps_mark = last;
    double accumulator = 0.0;
    int emulated = 0;
    bool f5_prev = false, f6_prev = false;   // tape transport edge detection

    // Audio: the beeper edge timeline resampled to PCM and played via miniaudio.
    // Only fed on the real-time (non-turbo) path, where one frame == 1/50 s of
    // samples; in turbo the emulation outruns the sound card.
    z80::audio::AudioOutput audio;
    const bool sound = !turbo && audio.start(44100);
    z80::machine::spectrum::BeeperResampler beeper(
        z80::machine::spectrum::timing::kCpuHz, sound ? audio.sample_rate() : 44100);
    std::vector<int16_t> samples;
    if (sound) std::cout << "sound: on (" << audio.sample_rate() << " Hz)\n";

    const auto pump_audio = [&] {
        if (!sound) return;
        samples.clear();
        for (const auto& e : machine.ula().beeper_edges())
            beeper.edge(e.cycle, e.level, samples);
        beeper.advance(machine.cpu().GetCycleCount(), samples);
        audio.push(samples);
    };

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        poll_keyboard(window, machine.ula());

        // Tape transport: F5 = play, F6 = stop (on key-down edge).
        const bool f5 = glfwGetKey(window, GLFW_KEY_F5) == GLFW_PRESS;
        const bool f6 = glfwGetKey(window, GLFW_KEY_F6) == GLFW_PRESS;
        if (f5 && !f5_prev) { machine.play_tape(); std::cout << "tape: play\n"; }
        if (f6 && !f6_prev) { machine.stop_tape(); std::cout << "tape: stop\n"; }
        f5_prev = f5;
        f6_prev = f6;

        const auto now = clock::now();
        const double dt = std::chrono::duration<double>(now - last).count();
        last = now;

        if (turbo) {
            machine.run_frame();
            ++emulated;
        } else {
            accumulator += dt;
            if (accumulator > 0.25) accumulator = 0.25;        // don't spiral after a stall
            int ran = 0;
            while (accumulator >= frame_period && ran < 4) {    // real-time, capped catch-up
                machine.run_frame();
                pump_audio();                                    // drain this frame's beeper edges
                accumulator -= frame_period;
                ++ran;
                ++emulated;
            }
        }

        machine.render_rgba(rgba);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, sm::video::kFrameWidth, sm::video::kFrameHeight,
                        GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        const ImVec2 size = ImGui::GetIO().DisplaySize;
        ImGui::GetBackgroundDrawList()->AddImage(
            reinterpret_cast<ImTextureID>(static_cast<intptr_t>(texture)),
            ImVec2(0, 0), size);

        ImGui::Render();
        int w, h;
        glfwGetFramebufferSize(window, &w, &h);
        glViewport(0, 0, w, h);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);

        const double since = std::chrono::duration<double>(now - fps_mark).count();
        if (since >= 1.0) {
            const double fps = emulated / since;
            glfwSetWindowTitle(window, std::format(
                "ZX Spectrum 48K — {:.0f} fps ({:.0f}% of 50 Hz){}",
                fps, fps / kHz * 100.0, turbo ? "  [turbo]" : "").c_str());
            std::cout << std::format("emulated {:.1f} fps ({:.0f}% of real){}\n",
                                     fps, fps / kHz * 100.0, turbo ? "  [turbo]" : "")
                      << std::flush;
            fps_mark = now;
            emulated = 0;
        }
    }

    glDeleteTextures(1, &texture);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
