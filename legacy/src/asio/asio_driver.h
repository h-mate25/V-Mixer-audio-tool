#pragma once

#include <Windows.h>
#include <atomic>
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "iasiodrv.h"
#include "../audio/audio_engine.h"
#include "../audio/output_monitor.h"
#include "../config/config.h"
#include "../reac/reac_receiver.h"
#include "../reac/reac_return_map.h"
#include "../reac/reac_transmitter.h"

namespace windows_reac_asio {

constexpr long kInputChannels = 40;
constexpr long kMonitorOutputChannels = 2;
constexpr long kMaxReacReturnChannels = static_cast<long>(kS1608ReturnWireChannels);
constexpr long kDefaultReacReturnChannels = 16;
constexpr long kMinBufferSize = 64;
constexpr long kMaxBufferSize = 2048;
constexpr long kPreferredBufferSize = 512;

extern const CLSID CLSID_WindowsReacASIO;
extern HMODULE gModuleHandle;
extern std::atomic<long> gObjectCount;
extern std::atomic<long> gServerLocks;

class WindowsReacAsioDriver final : public IASIO {
public:
    WindowsReacAsioDriver();
    ~WindowsReacAsioDriver();

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;

    ASIOBool init(void* sysHandle) override;
    void getDriverName(char* name) override;
    long getDriverVersion() override;
    void getErrorMessage(char* string) override;
    ASIOError start() override;
    ASIOError stop() override;
    ASIOError getChannels(long* numInputChannels, long* numOutputChannels) override;
    ASIOError getLatencies(long* inputLatency, long* outputLatency) override;
    ASIOError getBufferSize(long* minSize, long* maxSize, long* preferredSize, long* granularity) override;
    ASIOError canSampleRate(ASIOSampleRate sampleRate) override;
    ASIOError getSampleRate(ASIOSampleRate* sampleRate) override;
    ASIOError setSampleRate(ASIOSampleRate sampleRate) override;
    ASIOError getClockSources(ASIOClockSource* clocks, long* numSources) override;
    ASIOError setClockSource(long reference) override;
    ASIOError getSamplePosition(ASIOSamples* sPos, ASIOTimeStamp* tStamp) override;
    ASIOError getChannelInfo(ASIOChannelInfo* info) override;
    ASIOError createBuffers(ASIOBufferInfo* bufferInfos, long numChannels, long bufferSize, ASIOCallbacks* callbacks) override;
    ASIOError disposeBuffers() override;
    ASIOError controlPanel() override;
    ASIOError future(long selector, void* opt) override;
    ASIOError outputReady() override;

private:
    struct HostBuffer {
        long channel = 0;
        bool input = true;
        std::unique_ptr<float[]> storage;
    };

    void timerLoop();
    void bufferSwitch();
    void applyConfig(const DriverConfig& config, bool allowBufferResize);
    ASIOError stopInternal(bool stopReacTransmitter);
    bool restartWithConfig(const DriverConfig& config, std::wstring* errorMessage);
    void fillInputBuffers();
    void clearOutputBuffers();
    void writeReacReturnBuffers();
    void writeOutputMonitorBuffers();
    void updateAsioTime();
    static void copyString(char* destination, size_t capacity, const char* source) noexcept;
    static ASIOTimeStamp currentNanoSeconds() noexcept;

    std::atomic<ULONG> refCount_{1};
    std::atomic<bool> initialized_{false};
    std::atomic<bool> started_{false};
    std::atomic<bool> timerRunning_{false};
    std::thread timerThread_;

    AudioEngine audioEngine_;
    OutputMonitor outputMonitor_;
    ASIOCallbacks* callbacks_ = nullptr;
    ASIOTime asioTime_{};
    ASIOTimeStamp systemTime_{};
    double samplePosition_ = 0.0;
    double sampleRate_ = 48000.0;
    long blockFrames_ = kPreferredBufferSize;
    long inputChannelCount_ = kInputChannels;
    long reacReturnChannelCount_ = kDefaultReacReturnChannels;
    long monoReturnOutputChannels_ = 0;
    long monitorOutputOffset_ = kDefaultReacReturnChannels;
    long outputChannelCount_ = kDefaultReacReturnChannels + kMonitorOutputChannels;
    long inputLatency_ = kPreferredBufferSize;
    long outputLatency_ = kPreferredBufferSize;
    long toggle_ = 0;
    bool timeInfoMode_ = false;
    bool timeCodeRead_ = false;
    bool useReacReceiver_ = false;
    bool useWindowsOutputMonitor_ = true;
    bool receiveOnlyMode_ = false;
    bool enableReacOutput_ = false;
    bool exposeMonoReturns_ = false;
    uint32_t reacOutputBufferFrames_ = 8192;
    uint32_t reacReturnMap_ = kDefaultReacReturnMapMode;
    HWND hostWindow_ = nullptr;
    std::string reacInterfaceSelector_;
    char errorMessage_[128] = {};

    std::unique_ptr<ReacReceiver> reacReceiver_;
    std::unique_ptr<ReacTransmitter> reacTransmitter_;
    std::vector<HostBuffer> inputBuffers_;
    std::vector<HostBuffer> outputBuffers_;
    std::vector<float> reacReturnInterleaved_;
    std::vector<float> monitorInterleaved_;
    std::array<uint32_t, kInputChannels> activeInputChannels_{};
    std::array<float*, kInputChannels> activeInputDestinations_{};
};

} // namespace windows_reac_asio
