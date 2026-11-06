#include "pcap_sender.h"

#include <Windows.h>

#include <climits>

namespace windows_reac_asio {
namespace {

constexpr int kPcapErrorBufferSize = 256;

struct pcap_t;
using bpf_u_int32 = uint32_t;

struct pcap_timeval {
    long tv_sec;
    long tv_usec;
};

struct pcap_pkthdr {
    pcap_timeval ts;
    bpf_u_int32 caplen;
    bpf_u_int32 len;
};

struct pcap_send_queue {
    uint32_t maxlen;
    uint32_t len;
    char* buffer;
};

} // namespace

struct PcapSender::Impl {
    using pcap_open_live_fn = pcap_t* (*)(const char*, int, int, int, char*);
    using pcap_sendpacket_fn = int (*)(pcap_t*, const uint8_t*, int);
    using pcap_sendqueue_alloc_fn = pcap_send_queue* (*)(uint32_t);
    using pcap_sendqueue_destroy_fn = void (*)(pcap_send_queue*);
    using pcap_sendqueue_queue_fn = int (*)(pcap_send_queue*, const pcap_pkthdr*, const uint8_t*);
    using pcap_sendqueue_transmit_fn = uint32_t (*)(pcap_t*, pcap_send_queue*, int);
    using pcap_geterr_fn = char* (*)(pcap_t*);
    using pcap_close_fn = void (*)(pcap_t*);

    HMODULE library = nullptr;
    pcap_t* handle = nullptr;

    pcap_open_live_fn pcap_open_live = nullptr;
    pcap_sendpacket_fn pcap_sendpacket = nullptr;
    pcap_sendqueue_alloc_fn pcap_sendqueue_alloc = nullptr;
    pcap_sendqueue_destroy_fn pcap_sendqueue_destroy = nullptr;
    pcap_sendqueue_queue_fn pcap_sendqueue_queue = nullptr;
    pcap_sendqueue_transmit_fn pcap_sendqueue_transmit = nullptr;
    pcap_geterr_fn pcap_geterr = nullptr;
    pcap_close_fn pcap_close = nullptr;

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

PcapSender::PcapSender()
    : impl_(new Impl()) {
    impl_->library = LoadLibraryA("wpcap.dll");
    if (!impl_->library) {
        lastError_ = "Could not load wpcap.dll. Install Npcap and make sure it is available to desktop applications.";
        return;
    }

    if (!impl_->load(impl_->pcap_open_live, "pcap_open_live", lastError_) ||
        !impl_->load(impl_->pcap_sendpacket, "pcap_sendpacket", lastError_) ||
        !impl_->load(impl_->pcap_sendqueue_alloc, "pcap_sendqueue_alloc", lastError_) ||
        !impl_->load(impl_->pcap_sendqueue_destroy, "pcap_sendqueue_destroy", lastError_) ||
        !impl_->load(impl_->pcap_sendqueue_queue, "pcap_sendqueue_queue", lastError_) ||
        !impl_->load(impl_->pcap_sendqueue_transmit, "pcap_sendqueue_transmit", lastError_) ||
        !impl_->load(impl_->pcap_geterr, "pcap_geterr", lastError_) ||
        !impl_->load(impl_->pcap_close, "pcap_close", lastError_)) {
        FreeLibrary(impl_->library);
        impl_->library = nullptr;
    }
}

PcapSender::~PcapSender() {
    close();
    if (impl_->library) {
        FreeLibrary(impl_->library);
    }
    delete impl_;
}

PcapSender::TimedQueue::~TimedQueue() {
    if (impl_ && queue_ && impl_->pcap_sendqueue_destroy) {
        impl_->pcap_sendqueue_destroy(static_cast<pcap_send_queue*>(queue_));
    }
}

bool PcapSender::available() const noexcept {
    return impl_ && impl_->library && impl_->pcap_open_live && impl_->pcap_sendpacket;
}

bool PcapSender::open(const std::string& deviceName) {
    close();
    if (!available()) {
        return false;
    }

    char errorBuffer[kPcapErrorBufferSize] = {};
    impl_->handle = impl_->pcap_open_live(deviceName.c_str(), 65536, 1, 1, errorBuffer);
    if (!impl_->handle) {
        lastError_ = errorBuffer;
        return false;
    }

    lastError_.clear();
    return true;
}

bool PcapSender::send(const uint8_t* packet, size_t packetBytes) {
    if (!impl_ || !impl_->handle) {
        lastError_ = "Npcap sender is not open";
        return false;
    }
    if (!packet || packetBytes == 0 || packetBytes > static_cast<size_t>(INT_MAX)) {
        lastError_ = "Invalid packet passed to Npcap sender";
        return false;
    }

    if (impl_->pcap_sendpacket(impl_->handle, packet, static_cast<int>(packetBytes)) != 0) {
        lastError_ = impl_->pcap_geterr(impl_->handle);
        return false;
    }
    return true;
}

std::unique_ptr<PcapSender::TimedQueue> PcapSender::prepareTimedQueue(
    const std::vector<uint8_t>& packets,
    uint32_t packetBytes,
    uint32_t packetsPerSecond) {
    if (!impl_ || !impl_->library || !impl_->pcap_sendqueue_alloc || !impl_->pcap_sendqueue_queue) {
        lastError_ = "Npcap timed queue support is not available";
        return nullptr;
    }
    if (packetBytes == 0 || packetsPerSecond == 0 || packets.empty() || (packets.size() % packetBytes) != 0) {
        lastError_ = "Invalid packet queue";
        return nullptr;
    }

    const size_t packetCount = packets.size() / packetBytes;
    const size_t queueBytes = packets.size() + (packetCount * sizeof(pcap_pkthdr));
    if (queueBytes > static_cast<size_t>(UINT32_MAX)) {
        lastError_ = "Packet queue is too large for Npcap sendqueue";
        return nullptr;
    }

    pcap_send_queue* queue = impl_->pcap_sendqueue_alloc(static_cast<uint32_t>(queueBytes));
    if (!queue) {
        lastError_ = "pcap_sendqueue_alloc failed";
        return nullptr;
    }

    bool ok = true;
    for (size_t index = 0; index < packetCount; ++index) {
        const uint64_t timestampMicroseconds =
            (static_cast<uint64_t>(index) * 1000000ULL) / static_cast<uint64_t>(packetsPerSecond);

        pcap_pkthdr header{};
        header.ts.tv_sec = static_cast<long>(timestampMicroseconds / 1000000ULL);
        header.ts.tv_usec = static_cast<long>(timestampMicroseconds % 1000000ULL);
        header.caplen = packetBytes;
        header.len = packetBytes;

        if (impl_->pcap_sendqueue_queue(queue, &header, packets.data() + (index * packetBytes)) != 0) {
            lastError_ = "pcap_sendqueue_queue failed";
            ok = false;
            break;
        }
    }

    if (!ok) {
        impl_->pcap_sendqueue_destroy(queue);
        return nullptr;
    }

    return std::unique_ptr<TimedQueue>(new TimedQueue(impl_, queue));
}

bool PcapSender::transmitTimedQueue(const TimedQueue& queue) {
    if (!impl_ || !impl_->handle) {
        lastError_ = "Npcap sender is not open";
        return false;
    }
    pcap_send_queue* rawQueue = static_cast<pcap_send_queue*>(queue.queue_);
    if (!rawQueue) {
        lastError_ = "Invalid prepared packet queue";
        return false;
    }

    const uint32_t transmittedBytes = impl_->pcap_sendqueue_transmit(impl_->handle, rawQueue, 1);
    if (transmittedBytes < rawQueue->len) {
        lastError_ = impl_->pcap_geterr(impl_->handle);
        if (lastError_.empty()) {
            lastError_ = "pcap_sendqueue_transmit sent fewer bytes than queued";
        }
        return false;
    }
    return true;
}

bool PcapSender::sendTimedQueue(const std::vector<uint8_t>& packets, uint32_t packetBytes, uint32_t packetsPerSecond) {
    if (!impl_ || !impl_->handle) {
        lastError_ = "Npcap sender is not open";
        return false;
    }

    std::unique_ptr<TimedQueue> queue = prepareTimedQueue(packets, packetBytes, packetsPerSecond);
    if (!queue) {
        return false;
    }
    const bool ok = transmitTimedQueue(*queue);
    return ok;
}

void PcapSender::close() noexcept {
    if (impl_ && impl_->handle) {
        impl_->pcap_close(impl_->handle);
        impl_->handle = nullptr;
    }
}

} // namespace windows_reac_asio
