#ifndef REACSOCKET_H
#define REACSOCKET_H

#include <cstdint>
#include <pcap.h> // Needed for pcap_t

#ifdef __linux__ // Or a more general POSIX check if preferred
#include <net/ethernet.h> /* the L2 protocols */
#include <sys/select.h>   // For fd_set on Linux
#elif defined(__MINGW32__) || defined(_WIN32)
#include <winsock2.h>     // For fd_set and other socket functions on Windows
#include <ws2tcpip.h>     // For other Windows socket necessities
#ifndef ETHER_ADDR_LEN
#define ETHER_ADDR_LEN 6
#endif
#else
#include <net/ethernet.h> // Fallback for other systems (e.g. macOS)
#include <sys/select.h>   // For fd_set on macOS and other POSIX
#endif

class reacsocket
{
	pcap_t *pcap_handle; // For libpcap session
	int sock; // May become obsolete or used for pcap_get_selectable_fd()
	int ifindex; // May become less relevant with pcap device names
	uint8_t hwaddr[ETHER_ADDR_LEN];

	// for loop
	fd_set fds;
	int max_fds;

	public:
	int init_socket();
	int next_packet(uint8_t *buf, size_t buflen);
	void start_thread(class ringbuf &rb);

	// Constructor to initialize pcap_handle
	reacsocket() : pcap_handle(nullptr), sock(-1), ifindex(-1) { memset(hwaddr, 0, ETHER_ADDR_LEN); }
	// Destructor to close pcap handle
	~reacsocket();

	int get_socket() const { return sock; } // This might change meaning
	const uint8_t *get_hwaddr() const { return hwaddr; }
};

#endif // REACSOCKET_H
