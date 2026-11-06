#ifdef _WIN32
#include <pcap.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <interface>\n", argv[0]);
        return 1;
    }
    const char *if_name = argv[1];
    char errbuf[PCAP_ERRBUF_SIZE];
    pcap_t *p = pcap_open_live(if_name, 65536, 1, 1000, errbuf);
    if (!p) {
        fprintf(stderr, "pcap_open_live failed: %s\n", errbuf);
        return 2;
    }
    // Set BPF filter for REAC
    struct bpf_program fp;
    if (pcap_compile(p, &fp, "ether proto 0x8819", 1, PCAP_NETMASK_UNKNOWN) == -1) {
        fprintf(stderr, "Couldn't parse filter: %s\n", pcap_geterr(p));
        pcap_close(p);
        return 3;
    }
    if (pcap_setfilter(p, &fp) == -1) {
        fprintf(stderr, "Couldn't install filter: %s\n", pcap_geterr(p));
        pcap_freecode(&fp);
        pcap_close(p);
        return 4;
    }
    pcap_freecode(&fp);

    // Handler: write each packet to stdout (length + data)
    auto handler = [](u_char *, const struct pcap_pkthdr *h, const u_char *bytes) {
        uint32_t len = h->caplen;
        fwrite(&len, sizeof(len), 1, stdout);
        fwrite(bytes, 1, len, stdout);
        fflush(stdout);
    };

    fprintf(stderr, "Starting live capture on %s (Windows)...\n", if_name);
    int ret = pcap_loop(p, 0, handler, nullptr);
    pcap_close(p);
    return ret;
}
#endif 