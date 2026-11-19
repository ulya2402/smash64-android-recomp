#pragma once

#include <oboe/Oboe.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>

namespace smash64::audio {

class OboeAudioBackend : public oboe::AudioStreamDataCallback {
public:
    static constexpr int32_t kSampleRate = 48000;
    static constexpr int32_t kChannels = 2;
    static constexpr size_t kRingCapacitySamples = 32768;  // Power of two

    bool start();
    void stop();

    // Producer: called from N64 AI/recomp audio thread
    void pushSamples(const int16_t* interleavedStereo, size_t frameCount);

    // Consumer: real-time Oboe callback (zero allocation, zero locks)
    oboe::DataCallbackResult onAudioReady(oboe::AudioStream* audioStream,
                                          void* audioData,
                                          int32_t numFrames) override;

private:
    std::shared_ptr<oboe::AudioStream> stream_;
    std::array<int16_t, kRingCapacitySamples> ring_{};
    std::atomic<size_t> writeIdx_{0};
    std::atomic<size_t> readIdx_{0};
};

}  // namespace smash64::audio
