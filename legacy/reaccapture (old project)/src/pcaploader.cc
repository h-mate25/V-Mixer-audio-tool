#include <cstdio>
#include <cstdint>
#include <pcap.h>
#include <signal.h>

static bool is_interrupted = 0;
static pcap_t *p_int;

void interrupted(int)
{
	is_interrupted = 1;
	if (p_int) pcap_breakloop(p_int);
}

int load_pcap_offline(FILE *fp, int (*got_msg)(const uint8_t *, int))
{
	char errbuf[PCAP_ERRBUF_SIZE];

	pcap_t *p = pcap_fopen_offline(fp, errbuf);
	if(!p) {
		fputs(errbuf, stderr);
		return __LINE__;
	}

	struct pcap_pkthdr header;
	while(const uint8_t *payload = pcap_next(p, &header)) {
		got_msg(payload, header.caplen);
	}

	return 0;
}

int load_pcap_device(const char *if_name, int (*got_msg)(const uint8_t *, int))
{
	char errbuf[PCAP_ERRBUF_SIZE];

	pcap_t *p = pcap_create(if_name, errbuf);

	if(!p) {
		fputs(errbuf, stderr);
		return __LINE__;
	}

	pcap_set_promisc(p, 1);
	// TODO: pcap_set_buffer_size(p, TBD);

	pcap_activate(p);
	// TODO: consider to set a filter

	p_int = p;
	signal(SIGINT, interrupted);

	struct pcap_pkthdr header;
	while(const uint8_t *payload = pcap_next(p, &header)) {
		if (is_interrupted) break;
		got_msg(payload, header.caplen);
	}

	return 0;
}

#ifdef _WIN32
int load_pcap_device_win(const char *if_name, int (*got_msg)(const uint8_t *, int)) {
	char errbuf[PCAP_ERRBUF_SIZE];
	pcap_t *p = pcap_open_live(if_name, 65536, 1, 1000, errbuf);
	if (!p) {
		fprintf(stderr, "pcap_open_live failed: %s\n", errbuf);
		return 1;
	}

	// Set BPF filter for REAC
	struct bpf_program fp;
	if (pcap_compile(p, &fp, "ether proto 0x8819", 1, PCAP_NETMASK_UNKNOWN) == -1) {
		fprintf(stderr, "Couldn't parse filter: %s\n", pcap_geterr(p));
		pcap_close(p);
		return 2;
	}
	if (pcap_setfilter(p, &fp) == -1) {
		fprintf(stderr, "Couldn't install filter: %s\n", pcap_geterr(p));
		pcap_freecode(&fp);
		pcap_close(p);
		return 3;
	}
	pcap_freecode(&fp);

	// Packet handler
	struct CallbackData {
		int (*got_msg)(const uint8_t *, int);
	} cbdata = { got_msg };

	auto handler = [](u_char *user, const struct pcap_pkthdr *h, const u_char *bytes) {
		CallbackData *cb = (CallbackData *)user;
		cb->got_msg(bytes, h->caplen);
	};

	printf("Starting live capture on %s (Windows)...\n", if_name);
	int ret = pcap_loop(p, 0, handler, (u_char *)&cbdata);

	pcap_close(p);
	return ret;
}
#endif
