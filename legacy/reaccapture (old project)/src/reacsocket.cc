#include "reacsocket.h"
#include "ringbuf.h"
#include "settings.h"
#include "constants.h"

#include <cstdio>
#include <cstring>

#ifdef __linux__
#include <net/if.h>
#include <sys/ioctl.h>
#include <linux/if_packet.h>
#include <arpa/inet.h>
#elif defined(__MINGW32__) || defined(_WIN32)
#include <pcap.h>
#define IFNAMSIZ 16 // Common value for IFNAMSIZ
#include <iphlpapi.h> // For GetAdaptersAddresses
#pragma comment(lib, "Iphlpapi.lib") // Link against Iphlpapi.lib
#pragma comment(lib, "Ws2_32.lib") // Ensure Winsock library is linked
#endif

#include <pthread.h>

#ifdef __linux__
static inline
int bind_to_if(int fd, int ifindex)
{
	struct sockaddr_ll sa = {0};
	sa.sll_family = AF_PACKET;
	sa.sll_protocol = htons(ETH_P_ALL);
	sa.sll_ifindex = ifindex;
	if(bind(fd, (sockaddr *)&sa, sizeof(sa))<0) {
		perror("bind");
		return __LINE__;
	}
	return 0;
}

static inline
int set_promiscuous(int fd, int ifindex)
{
	struct packet_mreq mreq = {0};
	mreq.mr_type = PACKET_MR_PROMISC;
	mreq.mr_ifindex = ifindex;
	mreq.mr_alen = 0;
	mreq.mr_address[0] ='\\0';
	if(setsockopt(fd, SOL_PACKET, PACKET_ADD_MEMBERSHIP, (void *)&mreq, sizeof(mreq))<0) {
		perror("setsockopt PACKET_ADD_MEMBERSHIP");
		return __LINE__;
	}
	return 0;
}
#endif // __linux__

int reacsocket::init_socket()
{
#ifdef __linux__
	struct ifreq ifr = {0};

	strncpy(ifr.ifr_name, settings::if_name, IFNAMSIZ-1);

	// create socket
	sock = socket(PF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
	if(sock<0) {
		perror("socket");
		return __LINE__;
	}

	// TODO: Get interface index earlier than getting hwaddr.
	// TODO: Use interface index to get hwaddr.
	// SIOCGIFNAME: ifr_ifindex -> ifr_name

	// get hwaddr
	ifr.ifr_addr.sa_family = AF_INET; // This is unusual for PF_PACKET, usually not needed.
	if(ioctl(sock, SIOCGIFHWADDR, &ifr)<0) {
		perror("ioctl(SIOCGIFHWADDR)");
		return __LINE__;
	}
	memcpy(hwaddr, ifr.ifr_hwaddr.sa_data, ETHER_ADDR_LEN);
#ifdef DEBUG
	if(settings::verbose>2) fprintf(stderr, "interface address is %.2x:%.2x:%.2x:%.2x:%.2x:%.2x\\n",
			hwaddr[0], hwaddr[1], hwaddr[2],
			hwaddr[3], hwaddr[4], hwaddr[5] );
#endif // DEBUG
	uint8_t nul[ETHER_ADDR_LEN] = {0};
	if(!memcmp(hwaddr, nul, ETHER_ADDR_LEN)) {
		fprintf(stderr, "cannot get HW address\\n");
		return __LINE__;
	}

	// get interface index
	if(ioctl(sock, SIOCGIFINDEX, &ifr)<0) {
		perror("ioctl(SIOCGIFINDEX)");
		return __LINE__;
	}
	ifindex = ifr.ifr_ifindex;
#ifdef DEBUG
	if(settings::verbose>2) fprintf(stderr, "reacsocket[%p] interface index is %d\\n", this, ifindex);
#endif // DEBUG

	if(int r=bind_to_if(sock, ifindex)) return r;

	if(int r=set_promiscuous(sock, ifindex)) return r;

	// for loop
	FD_ZERO(&fds);
	FD_SET(sock, &fds);
	max_fds = sock+1;
	return 0;
#elif defined(__MINGW32__) || defined(_WIN32)
    char errbuf[PCAP_ERRBUF_SIZE];
    pcap_if_t *alldevs = nullptr;
    pcap_if_t *d = nullptr;
    bool device_found = false;

    // if (!settings::if_name) { // Allow to run without if_name to list interfaces
    //     fprintf(stderr, "Interface name not set.\\n");
    //     return __LINE__;
    // }

    // Find all available devices
    if (pcap_findalldevs(&alldevs, errbuf) == -1) {
        fprintf(stderr, "Error in pcap_findalldevs: %s\\n", errbuf);
        return __LINE__;
    }

    if (alldevs == nullptr) {
        fprintf(stderr, "pcap_findalldevs succeeded but found no interfaces. Ensure Npcap/WinPcap is installed, the service is running, and the application has administrator privileges. Pcap error (if any): [%s]\\n", errbuf[0] ? errbuf : "none");
        return __LINE__; // Or pcap_freealldevs(alldevs) first, though alldevs is NULL
    }

    fprintf(stderr, "Available interfaces:\\n");
    for (pcap_if_t *temp_d = alldevs; temp_d != nullptr; temp_d = temp_d->next) {
        fprintf(stderr, "  %s", temp_d->name);
        if (temp_d->description) {
            fprintf(stderr, " (%s)", temp_d->description);
        }
        fprintf(stderr, "\\n");
    }

    if (!settings::if_name) { // If no interface name was provided by user, exit after listing.
        fprintf(stderr, "No interface specified with -d. Listing available interfaces above.\\n");
        pcap_freealldevs(alldevs);
        return 1; // Indicate an issue or that the program can't proceed with capture
    }

    // Find the specified device
    for (d = alldevs; d != nullptr; d = d->next) {
        if (strcmp(settings::if_name, d->name) == 0) {
            device_found = true;
            break;
        }
    }

    if (!device_found || d == nullptr) {
        fprintf(stderr, "Interface %s not found.\\n", settings::if_name);
        if (alldevs) pcap_freealldevs(alldevs);
        return __LINE__;
    }

    // Get MAC address using GetAdaptersAddresses
    ULONG outBufLen = sizeof(IP_ADAPTER_ADDRESSES);
    PIP_ADAPTER_ADDRESSES pAddresses = (IP_ADAPTER_ADDRESSES*)malloc(outBufLen);
    if (pAddresses == NULL) {
        fprintf(stderr, "Memory allocation failed for GetAdaptersAddresses\\n");
        if (alldevs) pcap_freealldevs(alldevs);
        return __LINE__;
    }

    // Make an initial call to GetAdaptersAddresses to get the necessary buffer size
    if (GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_SKIP_FRIENDLY_NAME, NULL, pAddresses, &outBufLen) == ERROR_BUFFER_OVERFLOW) {
        free(pAddresses);
        pAddresses = (IP_ADAPTER_ADDRESSES*)malloc(outBufLen);
        if (pAddresses == NULL) {
            fprintf(stderr, "Memory allocation failed for GetAdaptersAddresses (2nd attempt)\\n");
            if (alldevs) pcap_freealldevs(alldevs);
            return __LINE__;
        }
    }

    bool mac_found = false;
    if (GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_SKIP_FRIENDLY_NAME, NULL, pAddresses, &outBufLen) == NO_ERROR) {
        for (PIP_ADAPTER_ADDRESSES pCurrAddresses = pAddresses; pCurrAddresses != nullptr; pCurrAddresses = pCurrAddresses->Next) {
            // Compare AdapterName (GUID) with the pcap device name
            // pcap device names on Windows are like \\Device\\NPF_{GUID}
            // We need to match the GUID part.
            const char* npf_prefix = "\\\\Device\\\\NPF_";
            if (strncmp(d->name, npf_prefix, strlen(npf_prefix)) == 0) {
                const char* pcap_guid = d->name + strlen(npf_prefix);
                // pCurrAddresses->AdapterName is a char* (narrow string) in some SDKs, wchar_t* in others.
                // Assuming it's char* for MinGW compatibility with AdapterName.
                // If wide char, conversion would be needed.
                // This comparison is tricky and might need refinement based on exact pcap and SDK versions.
                // For simplicity, let's assume settings::if_name is the pcap name like \\Device\\NPF_{GUID}
                // and we need to match this to the adapter that GetAdaptersAddresses gives us.
                // A more robust way is to iterate all pcap devices and all system adapters and match on description or friendly name if available.
                // This example directly tries to match the MAC of the found pcap device.

                // A more direct (but potentially less reliable for complex systems) MAC finding for pcap device:
                // Remove the loop that uses AF_PACKET_EX as it's not standard and GetAdaptersAddresses is preferred.
                // for (pcap_addr_t *addr = d->addresses; addr != nullptr; addr = addr->next) {
                //     if (addr->addr != nullptr && addr->addr->sa_family == AF_PACKET_EX) { 
                //         if (pCurrAddresses->PhysicalAddressLength == ETHER_ADDR_LEN) {
                //              memcpy(hwaddr, pCurrAddresses->PhysicalAddress, ETHER_ADDR_LEN);
                //              mac_found = true;
                //              break; 
                //         }
                //     }
                // }
                // if (mac_found) break; 
            }
             // Fallback: if the above specific check fails, iterate and pick the first valid MAC if settings::if_name matched one of pCurrAddresses->AdapterName.
            // This still requires robust matching between d->name and pCurrAddresses->AdapterName or FriendlyName.
            // This part of MAC address retrieval is the most complex for robust Windows support.
            // For this example, we assume the first MAC address from GetAdaptersAddresses is usable if no better match is found.
            // THIS IS A SIMPLIFICATION AND MAY NOT BE CORRECT.
            if (!mac_found && pCurrAddresses->PhysicalAddressLength == ETHER_ADDR_LEN) {
                 // This is a fallback if no specific match based on d->name's GUID was made.
                 // This typically picks the first adapter's MAC, which might be wrong.
                 // memcpy(hwaddr, pCurrAddresses->PhysicalAddress, ETHER_ADDR_LEN);
                 // mac_found = true; 
                 // break;
            }
        }
    }
     if (!mac_found) { // If loop completed and still no MAC that we are confident matches d->name
        // Attempt to get MAC from the first adapter found by GetAdaptersAddresses as a last resort (very unreliable)
        // Or, better, try to iterate pAddresses again and match d->Description more carefully if available.
        // For now, if no MAC from the sophisticated loop, try a simpler approach from d->addresses if available in a parseable format
        // This is often not reliable from pcap_if_t directly for MAC.
        // This whole MAC address section is the trickiest part for a robust solution.
        // For example purposes, if settings::if_name matches d->name, we are using 'd'.
        // Let's assume the GetAdaptersAddresses gave us *some* MAC in hwaddr earlier, or we clear it.
        // For now, we'll rely on the first MAC found from GetAdaptersAddresses matching the pcap device name (which is not guaranteed).
        // A real implementation needs to carefully map d->name (pcap's ID) to the correct adapter from GetAdaptersAddresses.
        // One way is to iterate all pcap devices, then for each, iterate all system adapters and match on description or friendly name.
        // For this example, we will assume that if we found 'd', GetAdaptersAddresses has provided *a* MAC.
        // If still no mac_found, it means the complex logic above failed to pinpoint one for 'd'.
        // For this exercise, we'll just use the first one found by GetAdaptersAddresses if the precise match failed.
        // This is often what happens in simpler examples, but it's not robust.
        // Let's assume pAddresses points to the list, and the first one has the MAC.
        if(pAddresses && pAddresses->PhysicalAddressLength == ETHER_ADDR_LEN && !mac_found){
             // This is a fallback and likely incorrect if there are multiple adapters.
             // memcpy(hwaddr, pAddresses->PhysicalAddress, ETHER_ADDR_LEN);
             // mac_found = true;
             // To avoid using a potentially wrong MAC, we should error out or leave hwaddr zeroed
             // if a confirmed match between pcap device 'd' and a system adapter's MAC wasn't made.
             fprintf(stderr, "Could not reliably determine MAC address for %s via GetAdaptersAddresses mapping.\\n", d->name);
             // For now, we'll clear hwaddr to indicate failure to get a specific MAC.
             memset(hwaddr, 0, ETHER_ADDR_LEN);


        } else if (!mac_found) {
             fprintf(stderr, "Failed to retrieve MAC address for %s or no matching adapter found.\\n", d->name);
             memset(hwaddr, 0, ETHER_ADDR_LEN); // Clear hwaddr
        }
    }


    if (pAddresses) free(pAddresses);

    // Check if hwaddr is all zeros (meaning we couldn't get it)
	uint8_t nul_hw[ETHER_ADDR_LEN] = {0};
	if(!memcmp(hwaddr, nul_hw, ETHER_ADDR_LEN)) {
		fprintf(stderr, "WARNING: Could not obtain MAC address for %s. Continuing without it, but some functionality might be affected.\\n", d->name);
		// It might be better to return an error here if MAC is critical
	} else {
    #ifdef DEBUG
        if(settings::verbose>2) fprintf(stderr, "interface address for %s is %.2x:%.2x:%.2x:%.2x:%.2x:%.2x\\n",
                d->name, hwaddr[0], hwaddr[1], hwaddr[2],
                hwaddr[3], hwaddr[4], hwaddr[5] );
    #endif
    }


    // Open the device for capturing
    // Parameters for pcap_open_live: device, snaplen, promisc, to_ms, errbuf
    pcap_handle = pcap_open_live(d->name, BUFSIZ, 1, 1000, errbuf);
    if (pcap_handle == nullptr) {
        fprintf(stderr, "Couldn't open device %s: %s\\n", d->name, errbuf);
        if (alldevs) pcap_freealldevs(alldevs);
        return __LINE__;
    }

    // Set non-blocking mode if needed for your application structure.
    // if (pcap_setnonblock(pcap_handle, 1, errbuf) == -1) {
    //     fprintf(stderr, "Error setting non-blocking mode: %s\\n", errbuf);
    //     pcap_close(pcap_handle); pcap_handle = nullptr;
    //     if (alldevs) pcap_freealldevs(alldevs);
    //     return __LINE__;
    // }
    
    // Get the selectable file descriptor
#ifdef HAVE_PCAP_GET_SELECTABLE_FD // Check if pcap_get_selectable_fd is available (autotools might define this)
    sock = pcap_get_selectable_fd(pcap_handle);
#else
    // If pcap_get_selectable_fd is not available or we are unsure, set sock to -1.
    // This means the select() based loop might not work efficiently for packet readiness.
    // pcap_next_ex() will then rely on its timeout or blocking behavior.
    fprintf(stderr, "Warning: pcap_get_selectable_fd is not available/detected. Packet polling might be inefficient or blocking.\n");
    sock = -1;
#endif
    // Note: sock might be -1 if pcap_get_selectable_fd is not supported or fails.
    // On Windows, this fd can be used with select().

    if (alldevs) pcap_freealldevs(alldevs); // Clean up device list

    FD_ZERO(&fds);
    if (sock != -1) { // Only add to fd_set if we have a valid descriptor
        FD_SET(sock, &fds);
        max_fds = sock + 1;
    } else {
        // If sock is -1, select() cannot be used with this pcap_handle directly in this way.
        // The application would need to use pcap_dispatch/pcap_loop in a blocking way
        // or use a different mechanism for packet readiness if pcap_get_selectable_fd() fails.
        // For now, we set max_fds to 0 to indicate no FDs for select.
        fprintf(stderr, "Warning: pcap_get_selectable_fd() returned -1. Select loop might not work as expected.\\n");
        max_fds = 0;
    }
    
    return 0;
#else
	#error "Unsupported platform for reacsocket::init_socket()"
	return 1;
#endif
}

int reacsocket::next_packet(uint8_t *buf, size_t buflen)
{
#ifdef __linux__
	struct sockaddr_ll rll = {0};
	socklen_t rll_size = sizeof(rll);
	int len = recvfrom(sock, buf, buflen, 0, (sockaddr*)&rll, &rll_size);
	return len;
#elif defined(__MINGW32__) || defined(_WIN32)
    if (!pcap_handle) {
        return -1;
    }
    struct pcap_pkthdr *header;
    const u_char *packet_data;
    int res = pcap_next_ex(pcap_handle, &header, &packet_data);

    if (res > 0) { // Packet read successfully
        size_t len_to_copy = header->caplen < buflen ? header->caplen : buflen;
        memcpy(buf, packet_data, len_to_copy);
        return len_to_copy;
    } else if (res == 0) {
        // Timeout elapsed if pcap_open_live was set with a timeout and non-blocking not fully set.
        // Or, if in non-blocking mode, no packet was available.
        return 0; 
    } else { // Error or EOF
        if (res == -1) fprintf(stderr, "Error reading packet: %s\\n", pcap_geterr(pcap_handle));
        // res == -2 means EOF for offline captures, not applicable here for live.
        return -1;
    }
#else
	#error "Unsupported platform for reacsocket::next_packet()"
	return -1;
#endif
}

struct routine_s
{
	ringbuf &rb;
	reacsocket &rs;
	routine_s(ringbuf &rb_, reacsocket &rs_) :rb(rb_), rs(rs_) {}
};

static
void *routine(void *arg)
{
	ringbuf &rb = ((routine_s*)arg)->rb;
	reacsocket &rs = ((routine_s*)arg)->rs;
	for(;;) {
		ringbuf::buf_s &b = rb.write_nextbuf();
		int ret = rs.next_packet(b.buf, RCV_MAX);
		if(ret>0) {
			b.len = ret;
			if(constants::check_protocol(b.buf)) {
				rb.wrote();
			}
		}
	}
	return NULL;
}

void reacsocket::start_thread(class ringbuf &rb)
{
	pthread_t p;
	routine_s *arg = new routine_s(rb, *this);
	pthread_create(&p, 0, routine, arg);
}

reacsocket::~reacsocket()
{
    if (pcap_handle) {
        pcap_close(pcap_handle);
        pcap_handle = nullptr;
    }
}
