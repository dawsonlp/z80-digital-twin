//
// Z80 Digital Twin - audio output (miniaudio backend) implementation
// Copyright (c) 2025 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//

#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_DECODING
#define MA_NO_ENCODING
#include "miniaudio.h"

#include "audio_output.h"

#include <cstring>

namespace z80::audio {

struct AudioOutput::Impl {
    ma_device device{};
    ma_pcm_rb rb{};
    bool device_ok = false;
    bool rb_ok = false;
    uint32_t rate = 0;
};

namespace {
void data_callback(ma_device* device, void* output, const void* /*input*/, ma_uint32 frames) {
    auto* impl = static_cast<AudioOutput::Impl*>(device->pUserData);
    auto* out = static_cast<int16_t*>(output);
    ma_uint32 remaining = frames;
    while (remaining > 0) {
        ma_uint32 n = remaining;
        void* buf = nullptr;
        if (ma_pcm_rb_acquire_read(&impl->rb, &n, &buf) != MA_SUCCESS || n == 0) break;
        std::memcpy(out, buf, static_cast<std::size_t>(n) * sizeof(int16_t));
        ma_pcm_rb_commit_read(&impl->rb, n);
        out += n;
        remaining -= n;
    }
    if (remaining > 0) std::memset(out, 0, static_cast<std::size_t>(remaining) * sizeof(int16_t));
}
} // namespace

AudioOutput::AudioOutput() : impl_(new Impl) {}

AudioOutput::~AudioOutput() {
    if (!impl_) return;
    if (impl_->device_ok) ma_device_uninit(&impl_->device);
    if (impl_->rb_ok) ma_pcm_rb_uninit(&impl_->rb);
    delete impl_;
}

bool AudioOutput::start(uint32_t sample_rate) {
    impl_->rate = sample_rate;

    // ~0.5 s of mono S16 headroom absorbs vsync/audio clock jitter.
    if (ma_pcm_rb_init(ma_format_s16, 1, sample_rate / 2, nullptr, nullptr, &impl_->rb) != MA_SUCCESS)
        return false;
    impl_->rb_ok = true;

    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.format = ma_format_s16;
    cfg.playback.channels = 1;
    cfg.sampleRate = sample_rate;
    cfg.dataCallback = data_callback;
    cfg.pUserData = impl_;

    if (ma_device_init(nullptr, &cfg, &impl_->device) != MA_SUCCESS) return false;
    impl_->device_ok = true;
    if (ma_device_start(&impl_->device) != MA_SUCCESS) return false;
    return true;
}

void AudioOutput::push(std::span<const int16_t> samples) {
    if (!impl_->rb_ok) return;
    std::size_t i = 0;
    while (i < samples.size()) {
        ma_uint32 n = static_cast<ma_uint32>(samples.size() - i);
        void* buf = nullptr;
        if (ma_pcm_rb_acquire_write(&impl_->rb, &n, &buf) != MA_SUCCESS || n == 0) break;  // full
        std::memcpy(buf, samples.data() + i, static_cast<std::size_t>(n) * sizeof(int16_t));
        ma_pcm_rb_commit_write(&impl_->rb, n);
        i += n;
    }
}

bool AudioOutput::active() const noexcept { return impl_ && impl_->device_ok; }
uint32_t AudioOutput::sample_rate() const noexcept { return impl_ ? impl_->rate : 0; }

} // namespace z80::audio
