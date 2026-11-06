#include "control_panel.h"

#include <algorithm>
#include <array>
#include <cwchar>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#include "../reac/pcap_capture.h"

namespace windows_reac_asio {
namespace {

constexpr wchar_t kWindowClassName[] = L"WindowsReacAsioControlPanel";

constexpr int kIdAdapterCombo = 1001;
constexpr int kIdRefreshDevices = 1002;
constexpr int kIdSampleRateCombo = 1003;
constexpr int kIdBufferSizeCombo = 1004;
constexpr int kIdJitterBufferCombo = 1005;
constexpr int kIdDebugLogCheck = 1006;
constexpr int kIdSave = 1007;
constexpr int kIdClose = 1008;
constexpr int kIdRefreshStats = 1009;
constexpr int kIdInputChannelCombo = 1010;
constexpr int kIdMonitorCheck = 1011;
constexpr int kIdRestartDriver = 1012;
constexpr int kIdReturnChannelCombo = 1013;
constexpr int kIdMonoReturnsCheck = 1014;
constexpr int kIdReacOutputCheck = 1015;
constexpr int kIdReturnMapCombo = 1016;
constexpr int kIdReadOnlyCheck = 1017;

std::wstring utf8ToWide(const std::string& value) {
    if (value.empty()) {
        return {};
    }
    const int length = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0);
    if (length <= 0) {
        return {};
    }
    std::wstring result(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), result.data(), length);
    return result;
}

std::wstring formatUint64(uint64_t value) {
    return std::to_wstring(value);
}

std::wstring formatFrames(uint64_t frames) {
    return std::to_wstring(frames);
}

uint32_t comboNumber(HWND combo, uint32_t fallback) {
    wchar_t text[64] = {};
    GetWindowTextW(combo, text, static_cast<int>(std::size(text)));
    wchar_t* end = nullptr;
    const unsigned long value = std::wcstoul(text, &end, 10);
    if (!end || end == text || *end != L'\0') {
        return fallback;
    }
    return static_cast<uint32_t>(value);
}

void setComboToNumber(HWND combo, uint32_t value) {
    const std::wstring wanted = std::to_wstring(value);
    const LRESULT index = SendMessageW(combo, CB_FINDSTRINGEXACT, static_cast<WPARAM>(-1), reinterpret_cast<LPARAM>(wanted.c_str()));
    if (index >= 0) {
        SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(index), 0);
    } else {
        SetWindowTextW(combo, wanted.c_str());
    }
}

void addComboNumber(HWND combo, uint32_t value) {
    const std::wstring text = std::to_wstring(value);
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
}

void addReturnMapMode(HWND combo, ReacReturnMapMode mode) {
    const LRESULT index = SendMessageW(
        combo,
        CB_ADDSTRING,
        0,
        reinterpret_cast<LPARAM>(reacReturnMapDisplayName(mode)));
    if (index >= 0) {
        SendMessageW(
            combo,
            CB_SETITEMDATA,
            static_cast<WPARAM>(index),
            static_cast<LPARAM>(reacReturnMapModeValue(mode)));
    }
}

void setComboToReturnMapMode(HWND combo, uint32_t mode) {
    const uint32_t wanted = reacReturnMapModeValue(normalizeReacReturnMapMode(mode));
    const LRESULT count = SendMessageW(combo, CB_GETCOUNT, 0, 0);
    for (LRESULT index = 0; index < count; ++index) {
        const LRESULT itemData = SendMessageW(combo, CB_GETITEMDATA, static_cast<WPARAM>(index), 0);
        if (itemData != CB_ERR && static_cast<uint32_t>(itemData) == wanted) {
            SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(index), 0);
            return;
        }
    }
    SendMessageW(combo, CB_SETCURSEL, 0, 0);
}

uint32_t comboReturnMapMode(HWND combo, uint32_t fallback) {
    const LRESULT index = SendMessageW(combo, CB_GETCURSEL, 0, 0);
    if (index < 0) {
        return fallback;
    }
    const LRESULT itemData = SendMessageW(combo, CB_GETITEMDATA, static_cast<WPARAM>(index), 0);
    if (itemData == CB_ERR) {
        return fallback;
    }
    return reacReturnMapModeValue(normalizeReacReturnMapMode(static_cast<uint32_t>(itemData)));
}

class ControlPanelWindow {
public:
    ControlPanelWindow(const ControlPanelState& state, DriverConfig* savedConfig)
        : state_(state)
        , config_(state.config)
        , savedConfig_(savedConfig) {
    }

    bool show(HWND parent) {
        parent_ = parent;
        instance_ = GetModuleHandleW(nullptr);
        registerClass();

        hwnd_ = CreateWindowExW(
            WS_EX_DLGMODALFRAME,
            kWindowClassName,
            L"Windows REAC ASIO Control Panel",
            WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            560,
            580,
            parent_,
            nullptr,
            instance_,
            this);

        if (!hwnd_) {
            return false;
        }

        if (parent_) {
            EnableWindow(parent_, FALSE);
        }

        MSG message = {};
        while (IsWindow(hwnd_) && GetMessageW(&message, nullptr, 0, 0) > 0) {
            if (!IsDialogMessageW(hwnd_, &message)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }

        if (parent_) {
            EnableWindow(parent_, TRUE);
            SetActiveWindow(parent_);
        }

        return saved_;
    }

private:
    static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        ControlPanelWindow* window = nullptr;
        if (message == WM_NCCREATE) {
            const CREATESTRUCTW* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
            window = static_cast<ControlPanelWindow*>(create->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
            window->hwnd_ = hwnd;
        } else {
            window = reinterpret_cast<ControlPanelWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        }

        if (!window) {
            return DefWindowProcW(hwnd, message, wParam, lParam);
        }
        return window->handleMessage(message, wParam, lParam);
    }

    void registerClass() const {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = ControlPanelWindow::windowProc;
        wc.hInstance = instance_;
        wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        wc.lpszClassName = kWindowClassName;
        RegisterClassExW(&wc);
    }

    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
        switch (message) {
        case WM_CREATE:
            createControls();
            return 0;
        case WM_COMMAND:
            handleCommand(LOWORD(wParam));
            return 0;
        case WM_CLOSE:
            DestroyWindow(hwnd_);
            return 0;
        case WM_DESTROY:
            hwnd_ = nullptr;
            return 0;
        default:
            return DefWindowProcW(hwnd_, message, wParam, lParam);
        }
    }

    HWND createControl(
        const wchar_t* className,
        const wchar_t* text,
        DWORD style,
        int x,
        int y,
        int width,
        int height,
        int id = 0) const {
        HWND control = CreateWindowExW(
            0,
            className,
            text,
            WS_CHILD | WS_VISIBLE | style,
            x,
            y,
            width,
            height,
            hwnd_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
            instance_,
            nullptr);
        if (control) {
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
        }
        return control;
    }

    HWND createStatic(const wchar_t* text, int x, int y, int width, int height) const {
        return createControl(L"STATIC", text, 0, x, y, width, height);
    }

    HWND createValueStatic(int x, int y, int width, int height) const {
        return createControl(L"STATIC", L"", SS_RIGHT, x, y, width, height);
    }

    void createControls() {
        font_ = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

        createControl(L"BUTTON", L"Source", BS_GROUPBOX, 12, 10, 520, 120);
        createStatic(L"Adapter", 28, 38, 80, 20);
        adapterCombo_ = createControl(
            L"COMBOBOX",
            L"",
            CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
            120,
            34,
            300,
            220,
            kIdAdapterCombo);
        createControl(L"BUTTON", L"Refresh", WS_TABSTOP, 430, 34, 82, 26, kIdRefreshDevices);
        sourceStatus_ = createStatic(L"", 120, 68, 390, 44);

        createControl(L"BUTTON", L"Audio", BS_GROUPBOX, 12, 140, 520, 150);
        createStatic(L"Sample rate", 28, 168, 100, 20);
        sampleRateCombo_ = createControl(
            L"COMBOBOX",
            L"",
            CBS_DROPDOWNLIST | WS_TABSTOP,
            140,
            164,
            120,
            120,
            kIdSampleRateCombo);
        createStatic(L"ASIO buffer", 282, 168, 92, 20);
        bufferSizeCombo_ = createControl(
            L"COMBOBOX",
            L"",
            CBS_DROPDOWNLIST | WS_TABSTOP,
            390,
            164,
            120,
            150,
            kIdBufferSizeCombo);
        createStatic(L"Jitter buffer", 28, 204, 100, 20);
        jitterBufferCombo_ = createControl(
            L"COMBOBOX",
            L"",
            CBS_DROPDOWNLIST | WS_TABSTOP,
            140,
            200,
            120,
            160,
            kIdJitterBufferCombo);
        createStatic(L"Input channels", 282, 204, 100, 20);
        inputChannelCombo_ = createControl(
            L"COMBOBOX",
            L"",
            CBS_DROPDOWNLIST | WS_TABSTOP,
            390,
            200,
            120,
            140,
            kIdInputChannelCombo);
        createStatic(L"REAC returns", 28, 240, 100, 20);
        returnChannelCombo_ = createControl(
            L"COMBOBOX",
            L"",
            CBS_DROPDOWNLIST | WS_TABSTOP,
            140,
            236,
            120,
            140,
            kIdReturnChannelCombo);
        createStatic(L"Return map", 282, 240, 100, 20);
        returnMapCombo_ = createControl(
            L"COMBOBOX",
            L"",
            CBS_DROPDOWNLIST | WS_TABSTOP,
            390,
            236,
            120,
            120,
            kIdReturnMapCombo);

        createControl(L"BUTTON", L"Diagnostics", BS_GROUPBOX, 12, 300, 520, 128);
        createStatic(L"Underruns", 28, 330, 120, 20);
        underrunValue_ = createValueStatic(160, 330, 90, 20);
        createStatic(L"Overflows", 282, 330, 110, 20);
        overflowValue_ = createValueStatic(410, 330, 90, 20);
        createStatic(L"Packet drops", 28, 360, 120, 20);
        packetDropValue_ = createValueStatic(160, 360, 90, 20);
        createStatic(L"Frames received", 282, 360, 120, 20);
        framesReceivedValue_ = createValueStatic(410, 360, 90, 20);
        createStatic(L"Frames delivered", 282, 390, 120, 20);
        framesDeliveredValue_ = createValueStatic(410, 390, 90, 20);
        createControl(L"BUTTON", L"Refresh stats", WS_TABSTOP, 28, 390, 110, 26, kIdRefreshStats);

        debugLogCheck_ = createControl(
            L"BUTTON",
            L"Enable debug output",
            BS_AUTOCHECKBOX | WS_TABSTOP,
            28,
            442,
            180,
            22,
            kIdDebugLogCheck);
        monitorCheck_ = createControl(
            L"BUTTON",
            L"Windows monitor out",
            BS_AUTOCHECKBOX | WS_TABSTOP,
            216,
            442,
            180,
            22,
            kIdMonitorCheck);
        monoReturnsCheck_ = createControl(
            L"BUTTON",
            L"Mono return pairs",
            BS_AUTOCHECKBOX | WS_TABSTOP,
            400,
            442,
            120,
            22,
            kIdMonoReturnsCheck);
        reacOutputCheck_ = createControl(
            L"BUTTON",
            L"REAC output",
            BS_AUTOCHECKBOX | WS_TABSTOP,
            28,
            468,
            160,
            22,
            kIdReacOutputCheck);
        readOnlyCheck_ = createControl(
            L"BUTTON",
            L"REAC input only",
            BS_AUTOCHECKBOX | WS_TABSTOP,
            216,
            468,
            180,
            22,
            kIdReadOnlyCheck);
        createControl(L"BUTTON", L"Save", WS_TABSTOP | BS_DEFPUSHBUTTON, 230, 504, 88, 28, kIdSave);
        createControl(L"BUTTON", L"Restart driver", WS_TABSTOP, 328, 504, 100, 28, kIdRestartDriver);
        createControl(L"BUTTON", L"Close", WS_TABSTOP, 438, 504, 76, 28, kIdClose);

        populateAudioControls();
        populateDevices();
        updateStats(state_.counters);
        SendMessageW(debugLogCheck_, BM_SETCHECK, config_.diagnostics.enableDebugLog ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(monitorCheck_, BM_SETCHECK, config_.monitor.enableWindowsOutput ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(monoReturnsCheck_, BM_SETCHECK, config_.transmit.exposeMonoReturns ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(reacOutputCheck_, BM_SETCHECK, config_.transmit.enableReacOutput ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(readOnlyCheck_, BM_SETCHECK, config_.receiver.readOnly ? BST_CHECKED : BST_UNCHECKED, 0);
        updateReadOnlyControls();
    }

    void populateAudioControls() const {
        for (uint32_t value : std::array<uint32_t, 3>{44100, 48000, 96000}) {
            addComboNumber(sampleRateCombo_, value);
        }
        for (uint32_t value : std::array<uint32_t, 6>{64, 128, 256, 512, 1024, 2048}) {
            addComboNumber(bufferSizeCombo_, value);
        }
        for (uint32_t value : std::array<uint32_t, 5>{2048, 4096, 8192, 16384, 32768}) {
            addComboNumber(jitterBufferCombo_, value);
        }
        for (uint32_t value : std::array<uint32_t, 5>{8, 16, 24, 32, 40}) {
            addComboNumber(inputChannelCombo_, value);
        }
        for (uint32_t value : std::array<uint32_t, 2>{kS1608ReturnBankChannels, kS1608ReturnWireChannels}) {
            addComboNumber(returnChannelCombo_, value);
        }
        addReturnMapMode(returnMapCombo_, ReacReturnMapMode::s1608MirrorFirst8);
        addReturnMapMode(returnMapCombo_, ReacReturnMapMode::direct);

        setComboToNumber(sampleRateCombo_, config_.audio.sampleRate);
        setComboToNumber(bufferSizeCombo_, config_.audio.asioBufferSize);
        setComboToNumber(jitterBufferCombo_, config_.audio.jitterBufferSamples);
        setComboToNumber(inputChannelCombo_, config_.audio.channelCount);
        setComboToNumber(returnChannelCombo_, config_.transmit.outputChannels);
        setComboToReturnMapMode(returnMapCombo_, config_.transmit.returnMap);
    }

    void populateDevices() {
        SendMessageW(adapterCombo_, CB_RESETCONTENT, 0, 0);
        deviceSelectors_.clear();
        deviceSelectors_.push_back({});
        SendMessageW(adapterCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Test sine / no REAC capture"));

        PcapCapture capture;
        if (!capture.available()) {
            SetWindowTextW(sourceStatus_, utf8ToWide(capture.lastError()).c_str());
            SendMessageW(adapterCombo_, CB_SETCURSEL, 0, 0);
            return;
        }

        const std::vector<CaptureDeviceInfo> devices = capture.listDevices();
        for (const CaptureDeviceInfo& device : devices) {
            deviceSelectors_.push_back(device.name);
            std::wstring label = utf8ToWide(device.description.empty() ? device.name : device.description);
            if (label.empty()) {
                label = L"(unnamed adapter)";
            }
            SendMessageW(adapterCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
        }

        const std::string configuredSelector = wideToUtf8(config_.network.interfaceName);
        const std::string resolvedDevice = capture.resolveDeviceName(configuredSelector, devices);
        selectConfiguredDevice(configuredSelector, resolvedDevice);

        std::wstring status = L"Current: ";
        if (state_.currentInterfaceSelector.empty()) {
            status += L"test sine source";
        } else {
            status += utf8ToWide(state_.currentInterfaceSelector);
        }
        if (state_.driverStarted) {
            status += state_.usingReacReceiver ? L" (REAC receiver running)" : L" (test producer running)";
        }
        SetWindowTextW(sourceStatus_, status.c_str());
    }

    void selectConfiguredDevice(const std::string& selector, const std::string& resolvedDevice) const {
        if (selector.empty()) {
            SendMessageW(adapterCombo_, CB_SETCURSEL, 0, 0);
            return;
        }

        for (size_t index = 1; index < deviceSelectors_.size(); ++index) {
            if (deviceSelectors_[index] == selector || deviceSelectors_[index] == resolvedDevice) {
                SendMessageW(adapterCombo_, CB_SETCURSEL, static_cast<WPARAM>(index), 0);
                return;
            }
        }

        SendMessageW(adapterCombo_, CB_SETCURSEL, 0, 0);
        std::wstring status = L"Configured adapter was not found: ";
        status += utf8ToWide(selector);
        SetWindowTextW(sourceStatus_, status.c_str());
    }

    void updateStats(const AudioCountersSnapshot& counters) const {
        SetWindowTextW(underrunValue_, formatUint64(counters.underrunCount).c_str());
        SetWindowTextW(overflowValue_, formatUint64(counters.overflowCount).c_str());
        SetWindowTextW(packetDropValue_, formatUint64(counters.packetDropCount).c_str());
        SetWindowTextW(framesReceivedValue_, formatFrames(counters.framesReceived).c_str());
        SetWindowTextW(framesDeliveredValue_, formatFrames(counters.framesDelivered).c_str());
    }

    void handleCommand(int id) {
        switch (id) {
        case kIdRefreshDevices:
            populateDevices();
            break;
        case kIdRefreshStats:
            if (state_.readCounters) {
                updateStats(state_.readCounters());
            }
            break;
        case kIdReadOnlyCheck:
            updateReadOnlyControls();
            break;
        case kIdSave:
            save(true);
            break;
        case kIdRestartDriver:
            restartDriver();
            break;
        case kIdClose:
            DestroyWindow(hwnd_);
            break;
        default:
            break;
        }
    }

    bool save(bool showMessage) {
        config_.audio.sampleRate = comboNumber(sampleRateCombo_, config_.audio.sampleRate);
        config_.audio.asioBufferSize = comboNumber(bufferSizeCombo_, config_.audio.asioBufferSize);
        config_.audio.jitterBufferSamples = comboNumber(jitterBufferCombo_, config_.audio.jitterBufferSamples);
        config_.audio.channelCount = comboNumber(inputChannelCombo_, config_.audio.channelCount);
        config_.transmit.outputChannels = comboNumber(returnChannelCombo_, config_.transmit.outputChannels);
        config_.transmit.returnMap = comboReturnMapMode(returnMapCombo_, config_.transmit.returnMap);
        config_.diagnostics.enableDebugLog = SendMessageW(debugLogCheck_, BM_GETCHECK, 0, 0) == BST_CHECKED;
        config_.monitor.enableWindowsOutput = SendMessageW(monitorCheck_, BM_GETCHECK, 0, 0) == BST_CHECKED;
        config_.receiver.readOnly = SendMessageW(readOnlyCheck_, BM_GETCHECK, 0, 0) == BST_CHECKED;
        config_.transmit.exposeMonoReturns = SendMessageW(monoReturnsCheck_, BM_GETCHECK, 0, 0) == BST_CHECKED;
        config_.transmit.enableReacOutput = SendMessageW(reacOutputCheck_, BM_GETCHECK, 0, 0) == BST_CHECKED;
        if (config_.receiver.readOnly) {
            config_.transmit.exposeMonoReturns = false;
            config_.transmit.enableReacOutput = false;
        }

        const LRESULT selected = SendMessageW(adapterCombo_, CB_GETCURSEL, 0, 0);
        if (selected > 0 && static_cast<size_t>(selected) < deviceSelectors_.size()) {
            config_.network.interfaceName = utf8ToWide(deviceSelectors_[static_cast<size_t>(selected)]);
        } else {
            config_.network.interfaceName.clear();
        }

        std::wstring error;
        if (!saveConfig(config_, &error)) {
            std::wstring message = L"Could not save configuration:\n";
            message += error.empty() ? configFilePath() : error;
            MessageBoxW(hwnd_, message.c_str(), L"Windows REAC ASIO", MB_OK | MB_ICONERROR);
            return false;
        }

        saved_ = true;
        if (savedConfig_) {
            *savedConfig_ = config_;
        }

        if (showMessage) {
            std::wstring message = L"Configuration saved to:\n";
            message += configFilePath();
            message += L"\n\nUse Restart driver for receiver/monitor/REAC output changes. Channel count and buffer changes may still require reopening the ASIO device in the host.";
            MessageBoxW(hwnd_, message.c_str(), L"Windows REAC ASIO", MB_OK | MB_ICONINFORMATION);
        }
        return true;
    }

    void updateReadOnlyControls() {
        const bool readOnly = SendMessageW(readOnlyCheck_, BM_GETCHECK, 0, 0) == BST_CHECKED;
        if (readOnly) {
            SendMessageW(reacOutputCheck_, BM_SETCHECK, BST_UNCHECKED, 0);
            SendMessageW(monoReturnsCheck_, BM_SETCHECK, BST_UNCHECKED, 0);
        }
        EnableWindow(reacOutputCheck_, readOnly ? FALSE : TRUE);
        EnableWindow(monoReturnsCheck_, readOnly ? FALSE : TRUE);
        EnableWindow(returnChannelCombo_, readOnly ? FALSE : TRUE);
        EnableWindow(returnMapCombo_, readOnly ? FALSE : TRUE);
    }

    void restartDriver() {
        if (!save(false)) {
            return;
        }
        if (!state_.restartDriver) {
            MessageBoxW(hwnd_, L"Driver restart is not available in this host state.", L"Windows REAC ASIO", MB_OK | MB_ICONWARNING);
            return;
        }

        std::wstring error;
        if (!state_.restartDriver(config_, &error)) {
            std::wstring message = L"Could not restart the driver.";
            if (!error.empty()) {
                message += L"\n";
                message += error;
            }
            MessageBoxW(hwnd_, message.c_str(), L"Windows REAC ASIO", MB_OK | MB_ICONERROR);
            return;
        }

        if (state_.readCounters) {
            updateStats(state_.readCounters());
        }
        MessageBoxW(
            hwnd_,
            L"Driver restarted.\n\nIf you changed the visible input channel count or ASIO buffer size, reopen the ASIO device in the DAW so the host asks for the new layout.",
            L"Windows REAC ASIO",
            MB_OK | MB_ICONINFORMATION);
    }

    const ControlPanelState& state_;
    DriverConfig config_;
    DriverConfig* savedConfig_ = nullptr;
    std::vector<std::string> deviceSelectors_;
    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    HWND parent_ = nullptr;
    HFONT font_ = nullptr;
    HWND adapterCombo_ = nullptr;
    HWND sourceStatus_ = nullptr;
    HWND sampleRateCombo_ = nullptr;
    HWND bufferSizeCombo_ = nullptr;
    HWND jitterBufferCombo_ = nullptr;
    HWND inputChannelCombo_ = nullptr;
    HWND returnChannelCombo_ = nullptr;
    HWND returnMapCombo_ = nullptr;
    HWND debugLogCheck_ = nullptr;
    HWND monitorCheck_ = nullptr;
    HWND monoReturnsCheck_ = nullptr;
    HWND reacOutputCheck_ = nullptr;
    HWND readOnlyCheck_ = nullptr;
    HWND underrunValue_ = nullptr;
    HWND overflowValue_ = nullptr;
    HWND packetDropValue_ = nullptr;
    HWND framesReceivedValue_ = nullptr;
    HWND framesDeliveredValue_ = nullptr;
    bool saved_ = false;
};

} // namespace

bool showControlPanel(HWND parent, const ControlPanelState& state, DriverConfig* savedConfig) {
    ControlPanelWindow window(state, savedConfig);
    return window.show(parent);
}

} // namespace windows_reac_asio
