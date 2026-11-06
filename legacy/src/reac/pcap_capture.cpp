#include "pcap_capture.h"

#include <Windows.h>
#include <Iphlpapi.h>
#include <Objbase.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iterator>
#include <memory>

namespace windows_reac_asio {
namespace {

constexpr int kPcapErrorBufferSize = 256;
using bpf_u_int32 = uint32_t;

struct pcap_t;
struct pcap_addr_t;
struct bpf_insn;

struct pcap_if_t {
    pcap_if_t* next;
    char* name;
    char* description;
    pcap_addr_t* addresses;
    uint32_t flags;
};

struct pcap_timeval {
    long tv_sec;
    long tv_usec;
};

struct pcap_pkthdr {
    pcap_timeval ts;
    bpf_u_int32 caplen;
    bpf_u_int32 len;
};

struct bpf_program {
    uint32_t bf_len;
    bpf_insn* bf_insns;
};

std::string lowercase(std::string value) {
    for (char& ch : value) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return value;
}

bool parseOneBasedIndex(const std::string& value, size_t maxValue, size_t& index) {
    char* end = nullptr;
    const unsigned long parsed = std::strtoul(value.c_str(), &end, 10);
    if (!end || *end != '\0' || parsed == 0 || parsed > maxValue) {
        return false;
    }
    index = static_cast<size_t>(parsed - 1);
    return true;
}

uint64_t packetTimestampNanoseconds(const pcap_pkthdr* header) noexcept {
    if (!header) {
        return 0;
    }
    return static_cast<uint64_t>(header->ts.tv_sec) * 1000000000ULL +
           static_cast<uint64_t>(header->ts.tv_usec) * 1000ULL;
}

} // namespace

struct PcapCapture::Impl {
    using pcap_findalldevs_fn = int (*)(pcap_if_t**, char*);
    using pcap_freealldevs_fn = void (*)(pcap_if_t*);
    using pcap_create_fn = pcap_t* (*)(const char*, char*);
    using pcap_set_snaplen_fn = int (*)(pcap_t*, int);
    using pcap_set_promisc_fn = int (*)(pcap_t*, int);
    using pcap_set_timeout_fn = int (*)(pcap_t*, int);
    using pcap_set_buffer_size_fn = int (*)(pcap_t*, int);
    using pcap_activate_fn = int (*)(pcap_t*);
    using pcap_compile_fn = int (*)(pcap_t*, bpf_program*, const char*, int, bpf_u_int32);
    using pcap_setfilter_fn = int (*)(pcap_t*, bpf_program*);
    using pcap_freecode_fn = void (*)(bpf_program*);
    using pcap_geterr_fn = char* (*)(pcap_t*);
    using pcap_next_ex_fn = int (*)(pcap_t*, pcap_pkthdr**, const uint8_t**);
    using pcap_close_fn = void (*)(pcap_t*);
    using pcap_getevent_fn = HANDLE (*)(pcap_t*);

    HMODULE library = nullptr;
    pcap_t* capture = nullptr;

    pcap_findalldevs_fn pcap_findalldevs = nullptr;
    pcap_freealldevs_fn pcap_freealldevs = nullptr;
    pcap_create_fn pcap_create = nullptr;
    pcap_set_snaplen_fn pcap_set_snaplen = nullptr;
    pcap_set_promisc_fn pcap_set_promisc = nullptr;
    pcap_set_timeout_fn pcap_set_timeout = nullptr;
    pcap_set_buffer_size_fn pcap_set_buffer_size = nullptr;
    pcap_activate_fn pcap_activate = nullptr;
    pcap_compile_fn pcap_compile = nullptr;
    pcap_setfilter_fn pcap_setfilter = nullptr;
    pcap_freecode_fn pcap_freecode = nullptr;
    pcap_geterr_fn pcap_geterr = nullptr;
    pcap_next_ex_fn pcap_next_ex = nullptr;
    pcap_close_fn pcap_close = nullptr;
    pcap_getevent_fn pcap_getevent = nullptr;

    template <typename Fn>
    bool load(Fn& fn, const char* name, std::string& error) {
        fn = reinterpret_cast<Fn>(GetProcAddress(library, name));
        if (!fn) {
            error = std::string("Npcap/wpcap is missing function: ") + name;
            return false;
        }
        return true;
    }
};

PcapCapture::PcapCapture()
    : impl_(new Impl()) {
    impl_->library = LoadLibraryA("wpcap.dll");
    if (!impl_->library) {
        lastError_ = "Could not load wpcap.dll. Install Npcap and make sure it is available to desktop applications.";
        return;
    }

    if (!impl_->load(impl_->pcap_findalldevs, "pcap_findalldevs", lastError_) ||
        !impl_->load(impl_->pcap_freealldevs, "pcap_freealldevs", lastError_) ||
        !impl_->load(impl_->pcap_create, "pcap_create", lastError_) ||
        !impl_->load(impl_->pcap_set_snaplen, "pcap_set_snaplen", lastError_) ||
        !impl_->load(impl_->pcap_set_promisc, "pcap_set_promisc", lastError_) ||
        !impl_->load(impl_->pcap_set_timeout, "pcap_set_timeout", lastError_) ||
        !impl_->load(impl_->pcap_set_buffer_size, "pcap_set_buffer_size", lastError_) ||
        !impl_->load(impl_->pcap_activate, "pcap_activate", lastError_) ||
        !impl_->load(impl_->pcap_compile, "pcap_compile", lastError_) ||
        !impl_->load(impl_->pcap_setfilter, "pcap_setfilter", lastError_) ||
        !impl_->load(impl_->pcap_freecode, "pcap_freecode", lastError_) ||
        !impl_->load(impl_->pcap_geterr, "pcap_geterr", lastError_) ||
        !impl_->load(impl_->pcap_next_ex, "pcap_next_ex", lastError_) ||
        !impl_->load(impl_->pcap_close, "pcap_close", lastError_) ||
        !impl_->load(impl_->pcap_getevent, "pcap_getevent", lastError_)) {
        FreeLibrary(impl_->library);
        impl_->library = nullptr;
    }
}

PcapCapture::~PcapCapture() {
    close();
    if (impl_->library) {
        FreeLibrary(impl_->library);
    }
    delete impl_;
}

bool PcapCapture::available() const noexcept {
    return impl_ && impl_->library && impl_->pcap_findalldevs && impl_->pcap_create;
}

std::vector<CaptureDeviceInfo> PcapCapture::listDevices() {
    std::vector<CaptureDeviceInfo> devices;
    if (!available()) {
        return devices;
    }

    pcap_if_t* allDevices = nullptr;
    char errorBuffer[kPcapErrorBufferSize] = {};
    if (impl_->pcap_findalldevs(&allDevices, errorBuffer) != 0) {
        lastError_ = errorBuffer;
        return devices;
    }

    for (pcap_if_t* device = allDevices; device; device = device->next) {
        CaptureDeviceInfo info;
        info.name = device->name ? device->name : "";
        const std::string friendlyName = windowsFriendlyInterfaceName(info.name);
        if (!friendlyName.empty()) {
            info.description = friendlyName;
            if (device->description && friendlyName != device->description) {
                info.description += " - ";
                info.description += device->description;
            }
        } else {
            info.description = device->description ? device->description : info.name;
        }
        devices.push_back(std::move(info));
    }

    impl_->pcap_freealldevs(allDevices);
    return devices;
}

std::string PcapCapture::resolveDeviceName(const std::string& selector, const std::vector<CaptureDeviceInfo>& devices) const {
    if (selector.empty()) {
        return {};
    }

    size_t index = 0;
    if (parseOneBasedIndex(selector, devices.size(), index)) {
        return devices[index].name;
    }

    for (const CaptureDeviceInfo& device : devices) {
        if (device.name == selector) {
            return device.name;
        }
    }

    const std::string needle = lowercase(selector);
    for (const CaptureDeviceInfo& device : devices) {
        if (lowercase(device.name).find(needle) != std::string::npos ||
            lowercase(device.description).find(needle) != std::string::npos) {
            return device.name;
        }
    }

    return {};
}

bool PcapCapture::open(const std::string& deviceName, const std::string& filterExpression) {
    close();
    if (!available()) {
        return false;
    }

    char errorBuffer[kPcapErrorBufferSize] = {};
    impl_->capture = impl_->pcap_create(deviceName.c_str(), errorBuffer);
    if (!impl_->capture) {
        lastError_ = errorBuffer;
        return false;
    }

    if (impl_->pcap_set_snaplen(impl_->capture, 65536) != 0) {
        lastError_ = "pcap_set_snaplen failed";
        close();
        return false;
    }
    if (impl_->pcap_set_promisc(impl_->capture, 1) != 0) {
        lastError_ = "pcap_set_promisc failed";
        close();
        return false;
    }
    if (impl_->pcap_set_timeout(impl_->capture, 44) != 0) {
        lastError_ = "pcap_set_timeout failed";
        close();
        return false;
    }
    impl_->pcap_set_buffer_size(impl_->capture, 4 * 256 * 1024);

    const int activateResult = impl_->pcap_activate(impl_->capture);
    if (activateResult != 0) {
        lastError_ = impl_->pcap_geterr(impl_->capture);
        close();
        return false;
    }

    bpf_program filter = {};
    const std::string effectiveFilter = filterExpression.empty() ? "ether proto 0x8819" : filterExpression;
    if (impl_->pcap_compile(impl_->capture, &filter, effectiveFilter.c_str(), 1, 0xffffffffU) != 0) {
        lastError_ = impl_->pcap_geterr(impl_->capture);
        close();
        return false;
    }

    const int filterResult = impl_->pcap_setfilter(impl_->capture, &filter);
    impl_->pcap_freecode(&filter);
    if (filterResult != 0) {
        lastError_ = impl_->pcap_geterr(impl_->capture);
        close();
        return false;
    }

    lastError_.clear();
    return true;
}

void PcapCapture::close() noexcept {
    if (impl_ && impl_->capture) {
        impl_->pcap_close(impl_->capture);
        impl_->capture = nullptr;
    }
}

bool PcapCapture::captureOnce(uint32_t waitMilliseconds, const PacketCallback& callback) {
    if (!impl_ || !impl_->capture) {
        return false;
    }

    HANDLE event = impl_->pcap_getevent(impl_->capture);
    if (event) {
        const DWORD waitResult = WaitForSingleObject(event, waitMilliseconds);
        if (waitResult == WAIT_TIMEOUT) {
            return true;
        }
        if (waitResult != WAIT_OBJECT_0) {
            lastError_ = "WaitForSingleObject failed while waiting for pcap data";
            return false;
        }
    }

    pcap_pkthdr* header = nullptr;
    const uint8_t* payload = nullptr;
    const int result = impl_->pcap_next_ex(impl_->capture, &header, &payload);
    if (result == 1 && header && payload) {
        return callback(CapturedPacket{payload, header->caplen, header->len, packetTimestampNanoseconds(header)});
    }
    if (result < 0) {
        lastError_ = impl_->pcap_geterr(impl_->capture);
        return false;
    }
    return true;
}

std::string wideToUtf8(const std::wstring& value) {
    if (value.empty()) {
        return {};
    }
    const int length = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (length <= 0) {
        return {};
    }
    std::string result(static_cast<size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), result.data(), length, nullptr, nullptr);
    return result;
}

std::string windowsFriendlyInterfaceName(const std::string& deviceName) {
    constexpr char kNpcapPrefix[] = "\\Device\\NPF_";
    std::string guidText = deviceName;
    if (guidText.rfind(kNpcapPrefix, 0) == 0) {
        guidText = guidText.substr(sizeof(kNpcapPrefix) - 1);
    }
    if (guidText.empty()) {
        return {};
    }

    std::wstring wideGuid;
    const int wideLength = MultiByteToWideChar(CP_UTF8, 0, guidText.c_str(), -1, nullptr, 0);
    if (wideLength <= 1) {
        return {};
    }
    wideGuid.resize(static_cast<size_t>(wideLength - 1));
    MultiByteToWideChar(CP_UTF8, 0, guidText.c_str(), -1, wideGuid.data(), wideLength);

    GUID guid{};
    if (FAILED(CLSIDFromString(wideGuid.c_str(), &guid))) {
        return {};
    }

    NET_LUID luid{};
    if (ConvertInterfaceGuidToLuid(&guid, &luid) != NO_ERROR) {
        return {};
    }

    wchar_t alias[IF_MAX_STRING_SIZE + 1] = {};
    if (ConvertInterfaceLuidToAlias(&luid, alias, static_cast<ULONG>(std::size(alias))) != NO_ERROR) {
        return {};
    }

    return wideToUtf8(alias);
}

} // namespace windows_reac_asio
