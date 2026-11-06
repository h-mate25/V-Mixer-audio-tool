#include "output_monitor.h"

#include <Windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <memory>

#include "../diagnostics/logger.h"

namespace windows_reac_asio {
namespace {

using Microsoft::WRL::ComPtr;

constexpr REFERENCE_TIME kRequestedBufferDuration = 1000000; // 100 ms

struct CoTaskMemDeleter {
    void operator()(void* pointer) const noexcept {
        CoTaskMemFree(pointer);
    }
};

using WaveFormatPtr = std::unique_ptr<WAVEFORMATEX, CoTaskMemDeleter>;

void releaseAudioBuffer(IAudioRenderClient* renderClient, UINT32 frames, DWORD flags) noexcept {
    if (renderClient && frames > 0) {
        renderClient->ReleaseBuffer(frames, flags);
    }
}

WAVEFORMATEXTENSIBLE makeFloatStereoFormat(uint32_t sampleRate) noexcept {
    WAVEFORMATEXTENSIBLE format = {};
    format.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    format.Format.nChannels = 2;
    format.Format.nSamplesPerSec = sampleRate;
    format.Format.wBitsPerSample = 32;
    format.Format.nBlockAlign = static_cast<WORD>(format.Format.nChannels * sizeof(float));
    format.Format.nAvgBytesPerSec = format.Format.nSamplesPerSec * format.Format.nBlockAlign;
    format.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    format.Samples.wValidBitsPerSample = 32;
    format.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
    format.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
    return format;
}

bool isFloatFormat(const WAVEFORMATEX& format) noexcept {
    if (format.wFormatTag == WAVE_FORMAT_IEEE_FLOAT && format.wBitsPerSample == 32) {
        return true;
    }
    if (format.wFormatTag == WAVE_FORMAT_EXTENSIBLE && format.cbSize >= sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)) {
        const auto& extensible = reinterpret_cast<const WAVEFORMATEXTENSIBLE&>(format);
        return extensible.SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT && format.wBitsPerSample == 32;
    }
    return false;
}

bool isPcmFormat(const WAVEFORMATEX& format) noexcept {
    if (format.wFormatTag == WAVE_FORMAT_PCM) {
        return true;
    }
    if (format.wFormatTag == WAVE_FORMAT_EXTENSIBLE && format.cbSize >= sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)) {
        const auto& extensible = reinterpret_cast<const WAVEFORMATEXTENSIBLE&>(format);
        return extensible.SubFormat == KSDATAFORMAT_SUBTYPE_PCM;
    }
    return false;
}

float clampSample(float sample) noexcept {
    if (sample > 1.0f) {
        return 1.0f;
    }
    if (sample < -1.0f) {
        return -1.0f;
    }
    return sample;
}

void writeOutputFrame(uint8_t* frame, const WAVEFORMATEX& format, float left, float right) noexcept {
    left = clampSample(left);
    right = clampSample(right);

    const uint16_t channels = std::max<uint16_t>(format.nChannels, 1);
    const bool floatFormat = isFloatFormat(format);
    const bool pcmFormat = isPcmFormat(format);

    for (uint16_t channel = 0; channel < channels; ++channel) {
        const float sample = channel == 0 ? left : channel == 1 ? right : 0.0f;
        uint8_t* target = frame + channel * (format.wBitsPerSample / 8);

        if (floatFormat && format.wBitsPerSample == 32) {
            std::memcpy(target, &sample, sizeof(float));
        } else if (pcmFormat && format.wBitsPerSample == 16) {
            const int16_t value = static_cast<int16_t>(std::lrint(sample * 32767.0f));
            std::memcpy(target, &value, sizeof(value));
        } else if (pcmFormat && format.wBitsPerSample == 24) {
            const int32_t value = static_cast<int32_t>(std::lrint(sample * 8388607.0f));
            target[0] = static_cast<uint8_t>(value & 0xff);
            target[1] = static_cast<uint8_t>((value >> 8) & 0xff);
            target[2] = static_cast<uint8_t>((value >> 16) & 0xff);
        } else if (pcmFormat && format.wBitsPerSample == 32) {
            const int32_t value = static_cast<int32_t>(std::lrint(sample * 2147483647.0f));
            std::memcpy(target, &value, sizeof(value));
        }
    }
}

bool renderFromRing(
    RingBuffer& ringBuffer,
    AudioCounters& counters,
    BYTE* output,
    UINT32 frames,
    const WAVEFORMATEX& format,
    std::vector<float>& left,
    std::vector<float>& right) noexcept {
    if (!output || frames == 0) {
        return false;
    }

    std::memset(output, 0, static_cast<size_t>(frames) * format.nBlockAlign);

    std::array<uint32_t, 2> channels{0, 1};
    std::array<float*, 2> destinations{left.data(), right.data()};
    ringBuffer.readPlanar(channels.data(), destinations.data(), 2, frames, counters);

    for (UINT32 frame = 0; frame < frames; ++frame) {
        writeOutputFrame(output + static_cast<size_t>(frame) * format.nBlockAlign, format, left[frame], right[frame]);
    }
    return true;
}

} // namespace

OutputMonitor::~OutputMonitor() {
    stop();
}

bool OutputMonitor::configure(const OutputMonitorConfig& config) {
    stop();
    config_ = config;
    if (config_.channelCount < 2) {
        config_.channelCount = 2;
    }
    if (config_.bufferFrames < 512) {
        config_.bufferFrames = 512;
    }
    counters_.reset();
    lastError_.clear();
    return ringBuffer_.configure(2, config_.bufferFrames);
}

bool OutputMonitor::start() {
    if (running_.exchange(true, std::memory_order_acq_rel)) {
        return true;
    }

    ringBuffer_.clear();
    thread_ = std::thread(&OutputMonitor::renderLoop, this);
    return true;
}

void OutputMonitor::stop() {
    running_.store(false, std::memory_order_release);
    if (thread_.joinable()) {
        thread_.join();
    }
}

bool OutputMonitor::writeInterleaved(const float* interleaved, uint32_t frames) noexcept {
    if (!running_.load(std::memory_order_acquire)) {
        return false;
    }
    return ringBuffer_.writeInterleaved(interleaved, frames, counters_);
}

void OutputMonitor::renderLoop() {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool comInitialized = SUCCEEDED(hr);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        lastError_ = "CoInitializeEx failed";
        Logger::debug("Windows monitor output failed: CoInitializeEx failed");
        running_.store(false, std::memory_order_release);
        return;
    }

    ComPtr<IMMDeviceEnumerator> enumerator;
    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
    if (FAILED(hr)) {
        lastError_ = "Could not create WASAPI device enumerator";
        Logger::debug("Windows monitor output failed: could not create WASAPI device enumerator");
        running_.store(false, std::memory_order_release);
        if (comInitialized) {
            CoUninitialize();
        }
        return;
    }

    ComPtr<IMMDevice> device;
    hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
    if (FAILED(hr)) {
        lastError_ = "Could not open the default Windows playback endpoint";
        Logger::debug("Windows monitor output failed: could not open default playback endpoint");
        running_.store(false, std::memory_order_release);
        if (comInitialized) {
            CoUninitialize();
        }
        return;
    }

    ComPtr<IAudioClient> audioClient;
    hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(audioClient.GetAddressOf()));
    if (FAILED(hr)) {
        lastError_ = "Could not activate WASAPI audio client";
        Logger::debug("Windows monitor output failed: could not activate WASAPI audio client");
        running_.store(false, std::memory_order_release);
        if (comInitialized) {
            CoUninitialize();
        }
        return;
    }

    WaveFormatPtr mixFormat;
    WAVEFORMATEX* rawMixFormat = nullptr;
    hr = audioClient->GetMixFormat(&rawMixFormat);
    if (FAILED(hr) || !rawMixFormat) {
        lastError_ = "Could not query Windows playback mix format";
        Logger::debug("Windows monitor output failed: could not query mix format");
        running_.store(false, std::memory_order_release);
        if (comInitialized) {
            CoUninitialize();
        }
        return;
    }
    mixFormat.reset(rawMixFormat);

    WAVEFORMATEXTENSIBLE requestedFormat = makeFloatStereoFormat(config_.sampleRate);
    WAVEFORMATEX* selectedFormat = &requestedFormat.Format;
    WAVEFORMATEX* closestRaw = nullptr;
    hr = audioClient->IsFormatSupported(AUDCLNT_SHAREMODE_SHARED, selectedFormat, &closestRaw);
    WaveFormatPtr closestFormat(closestRaw);
    if (hr != S_OK) {
        selectedFormat = mixFormat.get();
    }

    if (selectedFormat->nSamplesPerSec != config_.sampleRate) {
        Logger::debug("Windows monitor output sample rate differs from ASIO; set the Windows playback device to 48 kHz if pitch or timing is wrong");
    }

    hr = audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, kRequestedBufferDuration, 0, selectedFormat, nullptr);
    if (FAILED(hr)) {
        lastError_ = "Could not initialize Windows monitor output";
        Logger::debug("Windows monitor output failed: could not initialize audio client");
        running_.store(false, std::memory_order_release);
        if (comInitialized) {
            CoUninitialize();
        }
        return;
    }

    UINT32 bufferFrameCount = 0;
    audioClient->GetBufferSize(&bufferFrameCount);

    ComPtr<IAudioRenderClient> renderClient;
    hr = audioClient->GetService(IID_PPV_ARGS(&renderClient));
    if (FAILED(hr)) {
        lastError_ = "Could not get WASAPI render client";
        Logger::debug("Windows monitor output failed: could not get render client");
        running_.store(false, std::memory_order_release);
        if (comInitialized) {
            CoUninitialize();
        }
        return;
    }

    std::vector<float> left(bufferFrameCount, 0.0f);
    std::vector<float> right(bufferFrameCount, 0.0f);

    hr = audioClient->Start();
    if (FAILED(hr)) {
        lastError_ = "Could not start Windows monitor output";
        Logger::debug("Windows monitor output failed: could not start audio client");
        running_.store(false, std::memory_order_release);
        if (comInitialized) {
            CoUninitialize();
        }
        return;
    }

    while (running_.load(std::memory_order_acquire)) {
        UINT32 padding = 0;
        if (FAILED(audioClient->GetCurrentPadding(&padding))) {
            break;
        }

        const UINT32 framesAvailable = bufferFrameCount > padding ? bufferFrameCount - padding : 0;
        if (framesAvailable > 0) {
            BYTE* data = nullptr;
            hr = renderClient->GetBuffer(framesAvailable, &data);
            if (FAILED(hr)) {
                break;
            }

            const bool rendered = renderFromRing(ringBuffer_, counters_, data, framesAvailable, *selectedFormat, left, right);
            releaseAudioBuffer(renderClient.Get(), framesAvailable, rendered ? 0 : AUDCLNT_BUFFERFLAGS_SILENT);
        }

        Sleep(5);
    }

    audioClient->Stop();
    running_.store(false, std::memory_order_release);
    if (comInitialized) {
        CoUninitialize();
    }
}

} // namespace windows_reac_asio
