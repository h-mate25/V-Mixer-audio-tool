#include "asio_driver.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

#include "../audio/channel_map.h"
#include "control_panel.h"
#include "../config/config.h"
#include "../diagnostics/logger.h"
#include "../reac/pcap_capture.h"

namespace windows_reac_asio {
namespace {

constexpr double kTwoRaisedTo32 = 4294967296.0;
constexpr uint32_t kPreferredReacTransmitBufferFrames = 4096;

uint32_t clampInputChannelCount(uint32_t channelCount) noexcept {
    if (channelCount == 0) {
        return kInputChannels;
    }
    return std::min<uint32_t>(channelCount, kInputChannels);
}

uint32_t clampReturnChannelCount(uint32_t channelCount) noexcept {
    if (channelCount == 0) {
        return kDefaultReacReturnChannels;
    }
    return std::min<uint32_t>(channelCount, static_cast<uint32_t>(kMaxReacReturnChannels));
}

bool isSupportedSampleRate(ASIOSampleRate sampleRate) noexcept {
    return sampleRate == 44100.0 || sampleRate == 48000.0 || sampleRate == 96000.0;
}

void doubleToAsioSamples(double value, ASIOSamples* samples) noexcept {
    if (!samples) {
        return;
    }
    if (value >= kTwoRaisedTo32) {
        samples->hi = static_cast<unsigned long>(value / kTwoRaisedTo32);
        samples->lo = static_cast<unsigned long>(value - static_cast<double>(samples->hi) * kTwoRaisedTo32);
    } else {
        samples->hi = 0;
        samples->lo = static_cast<unsigned long>(value);
    }
}

void addClamped(float& target, float source) noexcept {
    const float value = target + source;
    target = std::max(-1.0f, std::min(1.0f, value));
}

float peakAbs(const float* samples, long frames) noexcept {
    float peak = 0.0f;
    if (!samples || frames <= 0) {
        return peak;
    }
    for (long frame = 0; frame < frames; ++frame) {
        peak = std::max(peak, std::fabs(samples[frame]));
    }
    return peak;
}

} // namespace

const CLSID CLSID_WindowsReacASIO = {
    0x8d86067a,
    0x75e4,
    0x4e9b,
    {0x85, 0xcc, 0x24, 0xe2, 0xd5, 0x81, 0x4e, 0x07}};

HMODULE gModuleHandle = nullptr;
std::atomic<long> gObjectCount{0};
std::atomic<long> gServerLocks{0};

WindowsReacAsioDriver::WindowsReacAsioDriver() {
    gObjectCount.fetch_add(1, std::memory_order_relaxed);
    copyString(errorMessage_, sizeof(errorMessage_), "No error");

    applyConfig(loadConfig(), true);
}

WindowsReacAsioDriver::~WindowsReacAsioDriver() {
    stopInternal(true);
    disposeBuffers();
    gObjectCount.fetch_sub(1, std::memory_order_relaxed);
}

HRESULT STDMETHODCALLTYPE WindowsReacAsioDriver::QueryInterface(REFIID riid, void** object) {
    if (!object) {
        return E_POINTER;
    }
    *object = nullptr;

    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, CLSID_WindowsReacASIO)) {
        *object = static_cast<IASIO*>(this);
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE WindowsReacAsioDriver::AddRef() {
    return refCount_.fetch_add(1, std::memory_order_relaxed) + 1;
}

ULONG STDMETHODCALLTYPE WindowsReacAsioDriver::Release() {
    const ULONG remaining = refCount_.fetch_sub(1, std::memory_order_acq_rel) - 1;
    if (remaining == 0) {
        delete this;
    }
    return remaining;
}

ASIOBool WindowsReacAsioDriver::init(void* sysHandle) {
    hostWindow_ = reinterpret_cast<HWND>(sysHandle);
    initialized_.store(true, std::memory_order_release);
    copyString(errorMessage_, sizeof(errorMessage_), "No error");
    return ASIOTrue;
}

void WindowsReacAsioDriver::getDriverName(char* name) {
    copyString(name, 32, "Windows REAC ASIO");
}

long WindowsReacAsioDriver::getDriverVersion() {
    return 0x00010000;
}

void WindowsReacAsioDriver::getErrorMessage(char* string) {
    copyString(string, 128, errorMessage_);
}

ASIOError WindowsReacAsioDriver::start() {
    if (!callbacks_) {
        copyString(errorMessage_, sizeof(errorMessage_), "ASIO buffers have not been created");
        return ASE_NotPresent;
    }
    if (started_.exchange(true, std::memory_order_acq_rel)) {
        return ASE_OK;
    }

    samplePosition_ = 0.0;
    toggle_ = 0;
    if (useReacReceiver_) {
        if (!reacReceiver_) {
            reacReceiver_ = std::make_unique<ReacReceiver>(audioEngine_, reacInterfaceSelector_);
        }
        if (!reacReceiver_->running() && !reacReceiver_->start()) {
            Logger::debug(std::string("REAC receiver did not start: ") + reacReceiver_->lastError());
        }
    } else {
        audioEngine_.startTestProducer();
    }
    if (useWindowsOutputMonitor_) {
        if (!outputMonitor_.running() && !outputMonitor_.start()) {
            Logger::debug(std::string("Windows monitor output did not start: ") + outputMonitor_.lastError());
        }
    }
    if (!receiveOnlyMode_ && enableReacOutput_ && useReacReceiver_ && !(reacTransmitter_ && reacTransmitter_->running())) {
        ReacTransmitterConfig txConfig;
        txConfig.interfaceSelector = reacInterfaceSelector_;
        txConfig.sampleRate = static_cast<uint32_t>(sampleRate_);
        txConfig.channelCount = static_cast<uint32_t>(reacReturnChannelCount_);
        txConfig.bufferFrames = reacOutputBufferFrames_;
        txConfig.returnMap = reacReturnMap_;

        reacTransmitter_ = std::make_unique<ReacTransmitter>();
        if (!reacTransmitter_->configure(txConfig)) {
            Logger::debug(std::string("REAC transmitter did not configure: ") + reacTransmitter_->lastError());
            reacTransmitter_.reset();
        } else if (!reacTransmitter_->start()) {
            Logger::debug(std::string("REAC transmitter did not start: ") + reacTransmitter_->lastError());
            reacTransmitter_.reset();
        }
    }
    timerRunning_.store(true, std::memory_order_release);
    timerThread_ = std::thread(&WindowsReacAsioDriver::timerLoop, this);
    return ASE_OK;
}

ASIOError WindowsReacAsioDriver::stop() {
    return stopInternal(false);
}

ASIOError WindowsReacAsioDriver::stopInternal(bool stopReacTransmitter) {
    started_.store(false, std::memory_order_release);
    timerRunning_.store(false, std::memory_order_release);
    if (timerThread_.joinable()) {
        timerThread_.join();
    }
    if ((stopReacTransmitter || receiveOnlyMode_ || !enableReacOutput_ || !useReacReceiver_) && reacTransmitter_) {
        reacTransmitter_->stop();
        reacTransmitter_.reset();
    }
    if ((stopReacTransmitter || !useReacReceiver_) && reacReceiver_) {
        reacReceiver_->stop();
        reacReceiver_.reset();
    }
    if (stopReacTransmitter || !useWindowsOutputMonitor_) {
        outputMonitor_.stop();
    }
    if (stopReacTransmitter || !useReacReceiver_) {
        audioEngine_.stopTestProducer();
    }
    return ASE_OK;
}

ASIOError WindowsReacAsioDriver::getChannels(long* numInputChannels, long* numOutputChannels) {
    if (!numInputChannels || !numOutputChannels) {
        return ASE_InvalidParameter;
    }
    *numInputChannels = inputChannelCount_;
    *numOutputChannels = outputChannelCount_;
    return ASE_OK;
}

ASIOError WindowsReacAsioDriver::getLatencies(long* inputLatency, long* outputLatency) {
    if (!inputLatency || !outputLatency) {
        return ASE_InvalidParameter;
    }
    *inputLatency = inputLatency_;
    *outputLatency = outputLatency_;
    return ASE_OK;
}

ASIOError WindowsReacAsioDriver::getBufferSize(long* minSize, long* maxSize, long* preferredSize, long* granularity) {
    if (!minSize || !maxSize || !preferredSize || !granularity) {
        return ASE_InvalidParameter;
    }
    *minSize = kMinBufferSize;
    *maxSize = kMaxBufferSize;
    *preferredSize = blockFrames_;
    *granularity = 0;
    return ASE_OK;
}

ASIOError WindowsReacAsioDriver::canSampleRate(ASIOSampleRate sampleRate) {
    return isSupportedSampleRate(sampleRate) ? ASE_OK : ASE_NoClock;
}

ASIOError WindowsReacAsioDriver::getSampleRate(ASIOSampleRate* sampleRate) {
    if (!sampleRate) {
        return ASE_InvalidParameter;
    }
    *sampleRate = sampleRate_;
    return ASE_OK;
}

ASIOError WindowsReacAsioDriver::setSampleRate(ASIOSampleRate sampleRate) {
    if (started_.load(std::memory_order_acquire)) {
        return ASE_InvalidMode;
    }
    if (!isSupportedSampleRate(sampleRate)) {
        return ASE_NoClock;
    }
    if (sampleRate_ == sampleRate) {
        return ASE_OK;
    }
    if (reacTransmitter_) {
        reacTransmitter_->stop();
        reacTransmitter_.reset();
    }

    sampleRate_ = sampleRate;
    AudioEngineConfig config = audioEngine_.config();
    config.sampleRate = static_cast<uint32_t>(sampleRate_);
    audioEngine_.configure(config);

    asioTime_.timeInfo.sampleRate = sampleRate_;
    asioTime_.timeInfo.flags |= kSampleRateChanged;
    if (callbacks_ && callbacks_->sampleRateDidChange) {
        callbacks_->sampleRateDidChange(sampleRate_);
    }
    return ASE_OK;
}

ASIOError WindowsReacAsioDriver::getClockSources(ASIOClockSource* clocks, long* numSources) {
    if (!clocks || !numSources || *numSources < 1) {
        return ASE_InvalidParameter;
    }
    clocks[0].index = 0;
    clocks[0].associatedChannel = -1;
    clocks[0].associatedGroup = -1;
    clocks[0].isCurrentSource = ASIOTrue;
    copyString(clocks[0].name, sizeof(clocks[0].name), "Internal");
    *numSources = 1;
    return ASE_OK;
}

ASIOError WindowsReacAsioDriver::setClockSource(long reference) {
    if (reference != 0) {
        return ASE_NotPresent;
    }
    asioTime_.timeInfo.flags |= kClockSourceChanged;
    return ASE_OK;
}

ASIOError WindowsReacAsioDriver::getSamplePosition(ASIOSamples* sPos, ASIOTimeStamp* tStamp) {
    if (!sPos || !tStamp) {
        return ASE_InvalidParameter;
    }
    doubleToAsioSamples(samplePosition_, sPos);
    *tStamp = systemTime_;
    return ASE_OK;
}

ASIOError WindowsReacAsioDriver::getChannelInfo(ASIOChannelInfo* info) {
    if (!info) {
        return ASE_InvalidParameter;
    }
    const long channelLimit = info->isInput ? inputChannelCount_ : outputChannelCount_;
    if (info->channel < 0 || info->channel >= channelLimit) {
        return ASE_InvalidParameter;
    }

    info->type = ASIOSTFloat32LSB;
    info->channelGroup = 0;
    info->isActive = ASIOFalse;

    if (info->isInput) {
        for (const HostBuffer& buffer : inputBuffers_) {
            if (buffer.channel == info->channel) {
                info->isActive = ASIOTrue;
                break;
            }
        }
        char name[32] = {};
        sprintf_s(name, "REAC Input %02ld", info->channel + 1);
        copyString(info->name, sizeof(info->name), name);
    } else {
        for (const HostBuffer& buffer : outputBuffers_) {
            if (buffer.channel == info->channel) {
                info->isActive = ASIOTrue;
                break;
            }
        }
        char name[32] = {};
        if (info->channel < reacReturnChannelCount_) {
            sprintf_s(name, "REAC Return %02ld", info->channel + 1);
        } else if (info->channel < monitorOutputOffset_) {
            const long monoOffset = info->channel - reacReturnChannelCount_;
            const long monoIndex = monoOffset / 2;
            const bool dummyRight = (monoOffset & 1L) != 0;
            sprintf_s(name, dummyRight ? "REAC Mono %02ld Dummy" : "REAC Mono %02ld", monoIndex + 1);
        } else {
            sprintf_s(name, "Monitor Out %02ld", info->channel - monitorOutputOffset_ + 1);
        }
        copyString(info->name, sizeof(info->name), name);
    }
    return ASE_OK;
}

ASIOError WindowsReacAsioDriver::createBuffers(
    ASIOBufferInfo* bufferInfos,
    long numChannels,
    long bufferSize,
    ASIOCallbacks* callbacks) {
    if (!bufferInfos || !callbacks || numChannels < 0 || bufferSize < kMinBufferSize || bufferSize > kMaxBufferSize) {
        return ASE_InvalidParameter;
    }

    stop();
    disposeBuffers();

    blockFrames_ = bufferSize;
    inputLatency_ = blockFrames_;
    outputLatency_ = blockFrames_;
    callbacks_ = callbacks;

    AudioEngineConfig config = audioEngine_.config();
    config.asioBufferSize = static_cast<uint32_t>(blockFrames_);
    audioEngine_.configure(config);

    inputBuffers_.reserve(inputChannelCount_);
    outputBuffers_.reserve(outputChannelCount_);
    reacReturnInterleaved_.assign(static_cast<size_t>(blockFrames_) * reacReturnChannelCount_, 0.0f);
    monitorInterleaved_.assign(static_cast<size_t>(blockFrames_) * kMonitorOutputChannels, 0.0f);

    for (long i = 0; i < numChannels; ++i) {
        ASIOBufferInfo& info = bufferInfos[i];
        const long limit = info.isInput ? inputChannelCount_ : outputChannelCount_;
        if (info.channelNum < 0 || info.channelNum >= limit) {
            disposeBuffers();
            return ASE_InvalidParameter;
        }

        HostBuffer buffer;
        buffer.channel = info.channelNum;
        buffer.input = info.isInput == ASIOTrue;
        buffer.storage = std::make_unique<float[]>(static_cast<size_t>(blockFrames_) * 2);
        std::fill(buffer.storage.get(), buffer.storage.get() + static_cast<size_t>(blockFrames_) * 2, 0.0f);
        info.buffers[0] = buffer.storage.get();
        info.buffers[1] = buffer.storage.get() + blockFrames_;

        if (info.isInput) {
            inputBuffers_.push_back(std::move(buffer));
        } else {
            outputBuffers_.push_back(std::move(buffer));
        }
    }

    if (callbacks_->asioMessage && callbacks_->asioMessage(kAsioSupportsTimeInfo, 0, nullptr, nullptr)) {
        timeInfoMode_ = true;
        asioTime_.timeInfo.speed = 1.0;
        asioTime_.timeInfo.sampleRate = sampleRate_;
        asioTime_.timeInfo.flags = kSystemTimeValid | kSamplePositionValid | kSampleRateValid;
        asioTime_.timeCode.speed = 1.0;
        asioTime_.timeCode.flags = kTcValid | kTcRunning;
    } else {
        timeInfoMode_ = false;
    }

    return ASE_OK;
}

ASIOError WindowsReacAsioDriver::disposeBuffers() {
    stopInternal(false);
    callbacks_ = nullptr;
    inputBuffers_.clear();
    outputBuffers_.clear();
    reacReturnInterleaved_.clear();
    monitorInterleaved_.clear();
    return ASE_OK;
}

ASIOError WindowsReacAsioDriver::controlPanel() {
    ControlPanelState state;
    state.config = loadConfig();
    state.config.audio.sampleRate = static_cast<uint32_t>(sampleRate_);
    state.config.audio.asioBufferSize = static_cast<uint32_t>(blockFrames_);
    state.config.audio.channelCount = static_cast<uint32_t>(inputChannelCount_);
    state.config.receiver.readOnly = receiveOnlyMode_;
    if (receiveOnlyMode_) {
        state.config.transmit.enableReacOutput = false;
        state.config.transmit.exposeMonoReturns = false;
    }
    state.counters = snapshotAudioCounters(audioEngine_.counters());
    state.readCounters = [this]() {
        return snapshotAudioCounters(audioEngine_.counters());
    };
    state.restartDriver = [this](const DriverConfig& config, std::wstring* errorMessage) {
        return restartWithConfig(config, errorMessage);
    };
    state.driverStarted = started_.load(std::memory_order_acquire);
    state.usingReacReceiver = useReacReceiver_;
    state.currentSampleRate = sampleRate_;
    state.currentBufferSize = blockFrames_;
    state.currentInterfaceSelector = reacInterfaceSelector_;

    DriverConfig savedConfig;
    if (showControlPanel(hostWindow_, state, &savedConfig) && !started_.load(std::memory_order_acquire)) {
        applyConfig(savedConfig, true);
    }
    return ASE_OK;
}

ASIOError WindowsReacAsioDriver::future(long selector, void* opt) {
    switch (selector) {
    case kAsioCanTimeInfo:
    case kAsioCanTimeCode:
    case kAsioCanInputMonitor:
    case kAsioSetInputMonitor:
        return ASE_SUCCESS;
    case kAsioEnableTimeCodeRead:
        timeCodeRead_ = true;
        return ASE_SUCCESS;
    case kAsioDisableTimeCodeRead:
        timeCodeRead_ = false;
        return ASE_SUCCESS;
    default:
        (void)opt;
        return ASE_NotPresent;
    }
}

ASIOError WindowsReacAsioDriver::outputReady() {
    return ASE_NotPresent;
}

void WindowsReacAsioDriver::timerLoop() {
    timeBeginPeriod(1);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    const auto period = std::chrono::duration<double>(static_cast<double>(blockFrames_) / sampleRate_);
    auto nextWake = std::chrono::steady_clock::now();

    while (timerRunning_.load(std::memory_order_acquire)) {
        bufferSwitch();
        nextWake += std::chrono::duration_cast<std::chrono::steady_clock::duration>(period);
        std::this_thread::sleep_until(nextWake);
    }

    timeEndPeriod(1);
}

void WindowsReacAsioDriver::bufferSwitch() {
    ASIOCallbacks* callbacks = callbacks_;
    if (!started_.load(std::memory_order_acquire) || !callbacks) {
        return;
    }

    systemTime_ = currentNanoSeconds();
    fillInputBuffers();
    clearOutputBuffers();
    samplePosition_ += blockFrames_;

    if (timeInfoMode_ && callbacks->bufferSwitchTimeInfo) {
        updateAsioTime();
        callbacks->bufferSwitchTimeInfo(&asioTime_, toggle_, ASIOFalse);
        asioTime_.timeInfo.flags &= ~(kSampleRateChanged | kClockSourceChanged);
    } else if (callbacks->bufferSwitch) {
        callbacks->bufferSwitch(toggle_, ASIOFalse);
    }

    writeReacReturnBuffers();
    writeOutputMonitorBuffers();
    toggle_ = toggle_ ? 0 : 1;
}

void WindowsReacAsioDriver::applyConfig(const DriverConfig& config, bool allowBufferResize) {
    AudioEngineConfig audioConfig = config.audio;
    audioConfig.channelCount = kInputChannels;
    if (!isSupportedSampleRate(static_cast<double>(audioConfig.sampleRate))) {
        audioConfig.sampleRate = 48000;
    }

    receiveOnlyMode_ = config.receiver.readOnly;
    inputChannelCount_ = static_cast<long>(clampInputChannelCount(config.audio.channelCount));
    reacReturnChannelCount_ = receiveOnlyMode_
                                  ? 0
                                  : static_cast<long>(clampReturnChannelCount(config.transmit.outputChannels));
    exposeMonoReturns_ = !receiveOnlyMode_ && config.transmit.exposeMonoReturns;
    monoReturnOutputChannels_ = exposeMonoReturns_ ? reacReturnChannelCount_ * 2 : 0;
    monitorOutputOffset_ = reacReturnChannelCount_ + monoReturnOutputChannels_;
    outputChannelCount_ = monitorOutputOffset_ + kMonitorOutputChannels;
    sampleRate_ = static_cast<double>(audioConfig.sampleRate);
    if (allowBufferResize) {
        blockFrames_ = static_cast<long>(std::clamp<uint32_t>(audioConfig.asioBufferSize, kMinBufferSize, kMaxBufferSize));
        inputLatency_ = blockFrames_;
        outputLatency_ = blockFrames_;
    }
    audioConfig.asioBufferSize = static_cast<uint32_t>(blockFrames_);

    audioEngine_.configure(audioConfig);
    reacInterfaceSelector_ = wideToUtf8(config.network.interfaceName);
    useReacReceiver_ = !reacInterfaceSelector_.empty();
    useWindowsOutputMonitor_ = config.monitor.enableWindowsOutput;
    enableReacOutput_ = !receiveOnlyMode_ && config.transmit.enableReacOutput;
    reacReturnMap_ = reacReturnMapModeValue(normalizeReacReturnMapMode(config.transmit.returnMap));
    const uint32_t preferredTransmitFrames = std::max<uint32_t>(
        audioConfig.asioBufferSize * 4,
        kPreferredReacTransmitBufferFrames);
    const uint32_t maximumTransmitFrames = std::max<uint32_t>(audioConfig.asioBufferSize * 8, preferredTransmitFrames);
    reacOutputBufferFrames_ = std::clamp<uint32_t>(
        config.transmit.bufferFrames,
        preferredTransmitFrames,
        maximumTransmitFrames);
    if ((receiveOnlyMode_ || !enableReacOutput_) && reacTransmitter_) {
        reacTransmitter_->stop();
        reacTransmitter_.reset();
    }

    OutputMonitorConfig monitorConfig;
    monitorConfig.sampleRate = static_cast<uint32_t>(sampleRate_);
    monitorConfig.bufferFrames = std::max<uint32_t>(config.monitor.bufferFrames, audioConfig.asioBufferSize * 4);
    outputMonitor_.configure(monitorConfig);
}

bool WindowsReacAsioDriver::restartWithConfig(const DriverConfig& config, std::wstring* errorMessage) {
    const bool wasStarted = started_.load(std::memory_order_acquire);
    stopInternal(true);
    applyConfig(config, false);

    if (!wasStarted) {
        return true;
    }

    const ASIOError result = start();
    if (result != ASE_OK) {
        if (errorMessage) {
            *errorMessage = L"ASIO start failed after restart.";
        }
        return false;
    }
    return true;
}

void WindowsReacAsioDriver::fillInputBuffers() {
    const uint32_t activeInputs = static_cast<uint32_t>(std::min<size_t>(inputBuffers_.size(), kInputChannels));
    for (uint32_t i = 0; i < activeInputs; ++i) {
        HostBuffer& buffer = inputBuffers_[i];
        activeInputChannels_[i] = hostInputToEngineChannel(static_cast<uint32_t>(buffer.channel));
        activeInputDestinations_[i] = buffer.storage.get() + (toggle_ ? blockFrames_ : 0);
    }
    audioEngine_.readInput(activeInputChannels_.data(), activeInputDestinations_.data(), activeInputs, static_cast<uint32_t>(blockFrames_));
}

void WindowsReacAsioDriver::clearOutputBuffers() {
    for (HostBuffer& buffer : outputBuffers_) {
        float* target = buffer.storage.get() + (toggle_ ? blockFrames_ : 0);
        std::fill(target, target + blockFrames_, 0.0f);
    }
}

void WindowsReacAsioDriver::writeReacReturnBuffers() {
    if (reacReturnChannelCount_ <= 0 || reacReturnInterleaved_.size() < static_cast<size_t>(blockFrames_) * reacReturnChannelCount_) {
        return;
    }

    for (long frame = 0; frame < blockFrames_; ++frame) {
        float* targetFrame = reacReturnInterleaved_.data() + static_cast<size_t>(frame) * reacReturnChannelCount_;
        std::fill(targetFrame, targetFrame + reacReturnChannelCount_, 0.0f);
    }

    for (HostBuffer& buffer : outputBuffers_) {
        long returnChannel = -1;
        if (buffer.channel >= 0 && buffer.channel < reacReturnChannelCount_) {
            returnChannel = buffer.channel;
        } else if (exposeMonoReturns_ && buffer.channel >= reacReturnChannelCount_ && buffer.channel < monitorOutputOffset_) {
            const long monoOffset = buffer.channel - reacReturnChannelCount_;
            if ((monoOffset & 1L) == 0) {
                returnChannel = monoOffset / 2;
            }
        }

        if (returnChannel < 0 || returnChannel >= reacReturnChannelCount_) {
            continue;
        }

        const float* source = buffer.storage.get() + (toggle_ ? blockFrames_ : 0);
        for (long frame = 0; frame < blockFrames_; ++frame) {
            float& target = reacReturnInterleaved_[static_cast<size_t>(frame) * reacReturnChannelCount_ + returnChannel];
            addClamped(target, source[frame]);
        }
    }

    if (enableReacOutput_ && reacReturnChannelCount_ >= 2) {
        bool returnHasSignal = false;
        for (long frame = 0; frame < blockFrames_ && !returnHasSignal; ++frame) {
            const float* frameData = reacReturnInterleaved_.data() + static_cast<size_t>(frame) * reacReturnChannelCount_;
            for (long channel = 0; channel < reacReturnChannelCount_; ++channel) {
                if (std::fabs(frameData[channel]) > 1.0e-7f) {
                    returnHasSignal = true;
                    break;
                }
            }
        }

        if (!returnHasSignal) {
            const HostBuffer* bestBuffer = nullptr;
            float bestPeak = 0.0f;
            for (HostBuffer& buffer : outputBuffers_) {
                const float* source = buffer.storage.get() + (toggle_ ? blockFrames_ : 0);
                const float peak = peakAbs(source, blockFrames_);
                if (peak > bestPeak) {
                    bestPeak = peak;
                    bestBuffer = &buffer;
                }
            }

            if (bestBuffer && bestPeak > 1.0e-7f) {
                const HostBuffer* leftBuffer = bestBuffer;
                const HostBuffer* rightBuffer = nullptr;
                const long companionChannel = (bestBuffer->channel & 1L) == 0L ? bestBuffer->channel + 1 : bestBuffer->channel - 1;
                for (HostBuffer& buffer : outputBuffers_) {
                    if (&buffer != bestBuffer && buffer.channel == companionChannel) {
                        rightBuffer = &buffer;
                        break;
                    }
                }
                if ((bestBuffer->channel & 1L) != 0L) {
                    rightBuffer = bestBuffer;
                    leftBuffer = nullptr;
                    for (HostBuffer& buffer : outputBuffers_) {
                        if (&buffer != bestBuffer && buffer.channel == companionChannel) {
                            leftBuffer = &buffer;
                            break;
                        }
                    }
                }

                const float* left = leftBuffer ? leftBuffer->storage.get() + (toggle_ ? blockFrames_ : 0) : nullptr;
                const float* right = rightBuffer ? rightBuffer->storage.get() + (toggle_ ? blockFrames_ : 0) : nullptr;
                for (long frame = 0; frame < blockFrames_; ++frame) {
                    float* targetFrame = reacReturnInterleaved_.data() + static_cast<size_t>(frame) * reacReturnChannelCount_;
                    targetFrame[0] = left ? left[frame] : 0.0f;
                    targetFrame[1] = right ? right[frame] : 0.0f;
                }
            }
        }
    }

    if (enableReacOutput_ && reacTransmitter_ && reacTransmitter_->running()) {
        reacTransmitter_->writeInterleaved(
            reacReturnInterleaved_.data(),
            static_cast<uint32_t>(reacReturnChannelCount_),
            static_cast<uint32_t>(blockFrames_));
    }
}

void WindowsReacAsioDriver::writeOutputMonitorBuffers() {
    if (!useWindowsOutputMonitor_ || outputBuffers_.empty() || monitorInterleaved_.size() < static_cast<size_t>(blockFrames_) * kMonitorOutputChannels) {
        return;
    }

    float* left = nullptr;
    float* right = nullptr;
    for (HostBuffer& buffer : outputBuffers_) {
        float* source = buffer.storage.get() + (toggle_ ? blockFrames_ : 0);
        if (buffer.channel == monitorOutputOffset_) {
            left = source;
        } else if (buffer.channel == monitorOutputOffset_ + 1) {
            right = source;
        }
    }

    for (long frame = 0; frame < blockFrames_; ++frame) {
        monitorInterleaved_[static_cast<size_t>(frame) * 2] = left ? left[frame] : 0.0f;
        monitorInterleaved_[static_cast<size_t>(frame) * 2 + 1] = right ? right[frame] : 0.0f;
    }

    outputMonitor_.writeInterleaved(monitorInterleaved_.data(), static_cast<uint32_t>(blockFrames_));
}

void WindowsReacAsioDriver::updateAsioTime() {
    getSamplePosition(&asioTime_.timeInfo.samplePosition, &asioTime_.timeInfo.systemTime);
    asioTime_.timeInfo.sampleRate = sampleRate_;
    asioTime_.timeInfo.flags |= kSystemTimeValid | kSamplePositionValid | kSampleRateValid;
    if (timeCodeRead_) {
        doubleToAsioSamples(samplePosition_ + 600.0 * sampleRate_, &asioTime_.timeCode.timeCodeSamples);
        asioTime_.timeCode.flags = kTcValid | kTcRunning;
    }
}

void WindowsReacAsioDriver::copyString(char* destination, size_t capacity, const char* source) noexcept {
    if (!destination || capacity == 0) {
        return;
    }
    if (!source) {
        source = "";
    }
    strncpy_s(destination, capacity, source, _TRUNCATE);
}

ASIOTimeStamp WindowsReacAsioDriver::currentNanoSeconds() noexcept {
    LARGE_INTEGER counter{};
    LARGE_INTEGER frequency{};
    QueryPerformanceCounter(&counter);
    QueryPerformanceFrequency(&frequency);
    const double nanos = (static_cast<double>(counter.QuadPart) * 1000000000.0) / static_cast<double>(frequency.QuadPart);
    ASIOTimeStamp timestamp{};
    timestamp.hi = static_cast<unsigned long>(nanos / kTwoRaisedTo32);
    timestamp.lo = static_cast<unsigned long>(nanos - static_cast<double>(timestamp.hi) * kTwoRaisedTo32);
    return timestamp;
}

} // namespace windows_reac_asio
