#include "audio_engine.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace windows_reac_asio {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr uint32_t kProducerBlockFrames = 128;

} // namespace

AudioEngine::AudioEngine() {
    configure(AudioEngineConfig{});
}

AudioEngine::~AudioEngine() {
    stopTestProducer();
}

bool AudioEngine::configure(const AudioEngineConfig& config) {
    stopTestProducer();
    config_ = config;
    counters_.reset();
    producerBlock_.assign(static_cast<size_t>(kProducerBlockFrames) * config_.channelCount, 0.0f);
    return ringBuffer_.configure(config_.channelCount, config_.jitterBufferSamples);
}

void AudioEngine::clear() noexcept {
    ringBuffer_.clear();
}

void AudioEngine::startTestProducer() {
    if (producerRunning_.exchange(true, std::memory_order_acq_rel)) {
        return;
    }
    ringBuffer_.clear();
    producerThread_ = std::thread(&AudioEngine::producerLoop, this);
}

void AudioEngine::stopTestProducer() {
    producerRunning_.store(false, std::memory_order_release);
    if (producerThread_.joinable()) {
        producerThread_.join();
    }
}

bool AudioEngine::writeInputInterleaved(const float* interleaved, uint32_t frames) noexcept {
    return ringBuffer_.writeInterleaved(interleaved, frames, counters_);
}

bool AudioEngine::writeInputSilence(uint32_t frames) noexcept {
    return ringBuffer_.writeSilence(frames, counters_);
}

void AudioEngine::readInput(
    const uint32_t* sourceChannels,
    float** destinationChannels,
    uint32_t destinationChannelCount,
    uint32_t frames) noexcept {
    ringBuffer_.readPlanar(sourceChannels, destinationChannels, destinationChannelCount, frames, counters_);
}

void AudioEngine::producerLoop() {
    uint64_t frameCursor = 0;
    const auto blockDuration = std::chrono::duration<double>(
        static_cast<double>(kProducerBlockFrames) / static_cast<double>(config_.sampleRate));
    auto nextWake = std::chrono::steady_clock::now();

    while (producerRunning_.load(std::memory_order_acquire)) {
        for (uint32_t frame = 0; frame < kProducerBlockFrames; ++frame) {
            const double t = static_cast<double>(frameCursor + frame) / static_cast<double>(config_.sampleRate);
            for (uint32_t channel = 0; channel < config_.channelCount; ++channel) {
                const double frequency = 220.0 + static_cast<double>(channel) * 5.0;
                const double level = channel == 0 ? 0.20 : 0.06;
                producerBlock_[static_cast<size_t>(frame) * config_.channelCount + channel] =
                    static_cast<float>(level * std::sin(2.0 * kPi * frequency * t));
            }
        }

        ringBuffer_.writeInterleaved(producerBlock_.data(), kProducerBlockFrames, counters_);
        frameCursor += kProducerBlockFrames;
        nextWake += std::chrono::duration_cast<std::chrono::steady_clock::duration>(blockDuration);
        std::this_thread::sleep_until(nextWake);
    }
}

} // namespace windows_reac_asio
