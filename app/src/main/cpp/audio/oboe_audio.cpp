#include "oboe_audio.h"

#include <android/log.h>

#include <algorithm>
#include <cstring>

#define LOG_TAG "Smash64Audio"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace smash64::audio {

bool OboeAudioBackend::start() {
    oboe::AudioStreamBuilder builder;
    builder.setDirection(oboe::Direction::Output)
        ->setPerformanceMode(oboe::PerformanceMode::LowLatency)
        ->setSharingMode(oboe::SharingMode::Exclusive)
        ->setFormat(oboe::AudioFormat::I16)
        ->setChannelCount(kChannels)
        ->setSampleRate(kSampleRate)
        ->setDataCallback(this);

    oboe::Result result = builder.openStream(stream_);
    if (result != oboe::Result::OK) {
        LOGE("Oboe openStream failed: %s", oboe::convertToText(result));
        return false;
    }

    result = stream_->requestStart();
    if (result != oboe::Result::OK) {
        LOGE("Oboe requestStart failed: %s", oboe::convertToText(result));
        return false;
    }

    LOGI("Oboe stream started: rate=%d burst=%d api=%d",
         stream_->getSampleRate(),
         stream_->getFramesPerBurst(),
         static_cast<int>(stream_->getAudioApi()));
    return true;
}

void OboeAudioBackend::stop() {
    if (stream_) {
        stream_->requestStop();
        stream_->close();
        stream_.reset();
    }
}

void OboeAudioBackend::pushSamples(const int16_t* interleavedStereo, size_t frameCount) {
    const size_t sampleCount = frameCount * kChannels;
    size_t w = writeIdx_.load(std::memory_order_relaxed);
    const size_t r = readIdx_.load(std::memory_order_acquire);

    for (size_t i = 0; i < sampleCount; ++i) {
        const size_t nextW = (w + 1) & (kRingCapacitySamples - 1);
        if (nextW == r) {
            break;  // Ring full
        }
        ring_[w] = interleavedStereo[i];
        w = nextW;
    }
    writeIdx_.store(w, std::memory_order_release);
}

oboe::DataCallbackResult OboeAudioBackend::onAudioReady(oboe::AudioStream* /*audioStream*/,
                                                        void* audioData,
                                                        int32_t numFrames) {
    auto* out = static_cast<int16_t*>(audioData);
    const size_t needed = static_cast<size_t>(numFrames) * kChannels;
    size_t r = readIdx_.load(std::memory_order_relaxed);
    const size_t w = writeIdx_.load(std::memory_order_acquire);

    size_t i = 0;
    while (i < needed && r != w) {
        out[i++] = ring_[r];
        r = (r + 1) & (kRingCapacitySamples - 1);
    }
    while (i < needed) {
        out[i++] = 0;
    }
    readIdx_.store(r, std::memory_order_release);
    return oboe::DataCallbackResult::Continue;
}

}  // namespace smash64::audio
