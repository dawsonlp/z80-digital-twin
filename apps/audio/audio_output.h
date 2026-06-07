//
// Z80 Digital Twin - audio output (miniaudio backend)
// Copyright (c) 2025-2026 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//
// A minimal mono S16 playback device fed from the main thread via a lock-free
// PCM ring buffer; the audio thread drains it (silence on underrun). The
// emulator pushes resampled beeper samples each frame. miniaudio is kept behind
// a pimpl so its (large) header stays out of callers.
//

#ifndef Z80_AUDIO_OUTPUT_H
#define Z80_AUDIO_OUTPUT_H

#include <cstdint>
#include <span>

namespace z80::audio {

class AudioOutput {
public:
    AudioOutput();
    ~AudioOutput();
    AudioOutput(const AudioOutput&) = delete;
    AudioOutput& operator=(const AudioOutput&) = delete;

    /// @brief Open and start the device. Returns false if no audio is available
    ///        (the caller then simply runs silent).
    bool start(uint32_t sample_rate);

    /// @brief Queue mono S16 samples for playback (drops if the buffer is full).
    void push(std::span<const int16_t> samples);

    [[nodiscard]] bool active() const noexcept;
    [[nodiscard]] uint32_t sample_rate() const noexcept;

    struct Impl;   // opaque (defined in the .cpp); public so the audio callback can name it

private:
    Impl* impl_ = nullptr;
};

} // namespace z80::audio

#endif // Z80_AUDIO_OUTPUT_H
