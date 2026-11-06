#include "reaccapture-config.h"
#include <cstdio>
#include <cstring>
#include <cassert>
#include <cstdlib>
#include <cctype>
#include <unistd.h>
#include <sys/time.h>
#include <cstdint>
#include <string>
#include <vector>
#include <iostream>
#include <sys/stat.h>
#include <time.h>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <array>

// to suppress warning
#include "mac-reacdriver/REACDataStream.h"
#undef ETHER_ADDR_LEN

#include "reacsocket.h"
#include "ringbuf.h"
#define SETTINGS_INSTANCIATE
#include "settings.h"
#undef SETTINGS_INSTANCIATE

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <direct.h> // For _mkdir
#define MKDIR(path) _mkdir(path)
#else
#define MKDIR(path) mkdir(path, 0755)
#endif

// instances
uint8_t interfaceAddr[ETHER_ADDR_LEN];
uint8_t master_addr[ETHER_ADDR_LEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
bool connected = 0;
enum HandshakeState {
	HANDSHAKE_NOT_INITIATED = 0,
	HANDSHAKE_GOT_MASTER_ANNOUNCE,
	HANDSHAKE_SENT_FIRST_ANNOUNCE,
	HANDSHAKE_GOT_SECOND_MASTER_ANNOUNCE,
	HANDSHAKE_CONNECTED
};
static HandshakeState g_handshake_state;
static unsigned int recievedPacketCounter = 0;
static int n_master_in_channels=0, n_master_out_channels=0;
static uint8_t splitIdentifier;
FILE *fp_outputs[40] = {0};
int n_skipped_counter = 0;
uint16_t counter_recv_last=0, counter_recv_last1=0;
uint32_t counter_recv_upper=0;
bool counter_recv_second = 0;
uint32_t n_samples = 0;

typedef int (*packet_handler_t)(const uint8_t *, int);

static
int write_packets(const uint8_t *data, int len)
{
	FILE *fp = stdout;
	fwrite(&len, sizeof(int), 1, fp);
	fwrite(data, len, 1, fp);
	return 0;
}

static int got_msg(const uint8_t *data_packet, int len_packet);

static
int load_packets(FILE *fp)
{
	int len;
	while(fread(&(len=0), sizeof(int), 1, fp)==1 && len>0) {
		uint8_t data[len];
		if(fread(data, len, 1, fp)!=1) {
			perror("fread");
			break;
		}
		got_msg(data, len);
	}
	return 0;
}

static FILE* fopen_output(int ch, const char *fmt)
{
	if(settings::of_name[ch]) {
		return fopen(settings::of_name[ch], "wb");
	}
	else {
		char name[64];
		snprintf(name, sizeof(name), fmt, ch+1);
		name[sizeof(name)-1] = 0;
		return fopen(name, "wb");
	}
}

static
int save_pcm_init()
{
	for(int i=0; i<40; i++) {
		if(settings::skip_channel & (1LL<<i)) continue;
		if(settings::stereo_channel & (1LL<<i) && i&1) {
			fp_outputs[i] = fp_outputs[i-1];
		}
		else {
			fp_outputs[i] = fopen_output(i, "output-%02d.pcm");
		}
	}
	return 0;
}

#define WAV_HEADER_SIZE 44

static
int save_wav_init()
{
	for(int i=0; i<40; i++) {
		if(settings::skip_channel & (1LL<<i)) continue;
		if(settings::stereo_channel & (1LL<<i) && i&1) {
			fp_outputs[i] = fp_outputs[i-1];
		}
		else {
			fp_outputs[i] = fopen_output(i, "output-%02d.wav");
			uint8_t data[WAV_HEADER_SIZE] = {"RIFF"};
			fwrite(data, WAV_HEADER_SIZE, 1, fp_outputs[i]);
		}
	}
	return 0;
}

static inline uint8_t* add32n(uint8_t *p, uint32_t n) { *p++=n&0xFF; *p++=(n>>8)&0xFF; *p++=(n>>16)&0xFF; *p++=(n>>24)&0xFF; return p; }
static inline uint8_t* add16n(uint8_t *p, uint16_t n) { *p++=n&0xFF; *p++=(n>>8)&0xFF; return p; }
static inline uint8_t* add32s(uint8_t *p, const char *s) { memcpy(p, s, 4); return p+4; }

static
int save_wav_fin()
{
	uint8_t headers[2][44];
	uint32_t n_byte_sample = settings::save_mode==save_wave24 ? 3 : settings::save_mode==save_wave16 ? 2 : -1;
	uint32_t n_data_byte = n_samples *n_byte_sample;
	for(uint32_t n_channel=1; n_channel<=2; n_channel++) {
		uint8_t *p = headers[n_channel-1];

		// RIFF header
		p = add32s(p, "RIFF");
		p = add32n(p, n_data_byte*n_channel+WAV_HEADER_SIZE-8); // size

		// WAVE header
		p = add32s(p, "WAVE");

		// fmt chunk
		p = add32s(p, "fmt ");
		p = add32n(p, 16); // size of fmt chunk
		p = add16n(p, 1); // format: 1=linear PCM
		p = add16n(p, n_channel); // #channels: 1=mono, 2=stereo
		p = add32n(p, 48000); // sampling rate
		p = add32n(p, n_byte_sample*n_channel*48000); // data rate [B/s]
		p = add16n(p, n_byte_sample*n_channel); // block size [B/sample], if stereo, make it twice.
		p = add16n(p, n_byte_sample*8); // bit/sample, independent to stereo or mono.

		// data chunk
		p = add32s(p, "data");
		p = add32n(p, n_data_byte*n_channel);
	}

	for(int i=0; i<40; i++) if(fp_outputs[i]) {
		uint32_t n_channel = (settings::stereo_channel & (1LL<<i)) ? 2 : 1;
		if(n_channel==2 && i&1) continue;
		FILE *fp = fp_outputs[i];
		fseek(fp, 0, SEEK_SET);
		fwrite(headers[n_channel-1], 44, 1, fp);
		fclose(fp); fp_outputs[i]=0;
	}
	return 0;
}

static
void save_pcm_data_s24be(const uint8_t *data, int skipped)
{
	if(skipped) {
		int len = skipped * 12 * 3;
		uint8_t x[len]; memset(x, 0, len);
		fprintf(stderr, "padding %d samples\n", len/3);
		for(int i=0; i<40; i++)
			if(fp_outputs[i]) fwrite(x, len, 1, fp_outputs[i]);
	}

	for(int j=0; j<12; j++) {
		for(int i=0; i<40; i+=2) {
			uint8_t x[3] = {data[1], data[0], data[3]};
			uint8_t y[3] = {data[2], data[5], data[4]};
			if(fp_outputs[i+0]) fwrite(x, 3, 1, fp_outputs[i+0]);
			if(fp_outputs[i+1]) fwrite(y, 3, 1, fp_outputs[i+1]);
			data += 6;
		}
	}
}

static
void save_pcm_data_s24le(const uint8_t *data, int skipped)
{
	if(skipped) {
		int len = skipped * 12 * 3;
		uint8_t x[len]; memset(x, 0, len);
		fprintf(stderr, "padding %d samples\n", len/3);
		for(int i=0; i<40; i++)
			if(fp_outputs[i]) fwrite(x, len, 1, fp_outputs[i]);
	}

	for(int j=0; j<12; j++) {
		for(int i=0; i<40; i+=2) {
			uint8_t x[3] = {data[3], data[0], data[1]};
			uint8_t y[3] = {data[4], data[5], data[2]};
			if(fp_outputs[i+0]) fwrite(x, 3, 1, fp_outputs[i+0]);
			if(fp_outputs[i+1]) fwrite(y, 3, 1, fp_outputs[i+1]);
			data += 6;
		}
	}
}

static
void save_pcm_data_s16be(const uint8_t *data, int skipped)
{
	if(skipped) {
		int len = skipped * 12 * 2;
		uint8_t x[len]; memset(x, 0, len);
		fprintf(stderr, "padding %d samples\n", len/3);
		for(int i=0; i<40; i++)
			if(fp_outputs[i]) fwrite(x, len, 1, fp_outputs[i]);
	}

	for(int j=0; j<12; j++) {
		for(int i=0; i<40; i+=2) {
			uint8_t x[2] = {data[1], data[0]};
			uint8_t y[2] = {data[2], data[5]};
			if(fp_outputs[i+0]) fwrite(x, 2, 1, fp_outputs[i+0]);
			if(fp_outputs[i+1]) fwrite(y, 2, 1, fp_outputs[i+1]);
			data += 6;
		}
	}
}

static
void save_pcm_data_s16le(const uint8_t *data, int skipped)
{
	if(skipped) {
		int len = skipped * 12 * 2;
		uint8_t x[len]; memset(x, 0, len);
		fprintf(stderr, "padding %d samples\n", len/3);
		for(int i=0; i<40; i++)
			if(fp_outputs[i]) fwrite(x, len, 1, fp_outputs[i]);
	}

	for(int j=0; j<12; j++) {
		for(int i=0; i<40; i+=2) {
			uint8_t x[2] = {data[0], data[1]};
			uint8_t y[2] = {data[5], data[2]};
			if(fp_outputs[i+0]) fwrite(x, 2, 1, fp_outputs[i+0]);
			if(fp_outputs[i+1]) fwrite(y, 2, 1, fp_outputs[i+1]);
			data += 6;
		}
	}
}

static uint8_t check_checksum(const REACPacketHeader *packet)
{
	uint8_t sum = 0;
	for (int i=0; i<32; i++)
		sum += packet->data[i];
	return sum;
}

static void apply_checksum(REACPacketHeader *packet)
{
	uint8_t sum = 0;
	for (int i=0; i<31; i++)
		sum += packet->data[i];
	packet->data[31] = -sum;
}

static void split_got_packet(const REACPacketHeader *packetHeader);

static
int got_msg(const uint8_t *data_packet, int len_packet)
{
#define ETHER_HEADER_LEN (6*2+2)
	const EthernetHeader *ethernetHeader = (EthernetHeader*)data_packet;

	// REACConnection::filterInputFunc
	if(memcmp(ethernetHeader->type, REACConstants::PROTOCOL, sizeof(ethernetHeader->type))) {
#ifdef DEBUG
		fprintf(stderr, "got_msg: ignoring type=%.2x:%.2x\n", ethernetHeader->type[0], ethernetHeader->type[1]);
#endif // DEBUG
		return 0;
	}

	recievedPacketCounter++;

	const uint8_t *data = data_packet + ETHER_HEADER_LEN;
	int len = len_packet - ETHER_HEADER_LEN;
#ifdef DEBUG
	printf("recvfrom returns %d len=%d:", len_packet, len);
	for(int i=0; i<len_packet && i<len_packet; i++) { printf(" %02X", data_packet[i]); } printf("\n");
#endif // DEBUG
	// TODO: why not check destination address?
	// TODO: see REACConnection::filterCommandGateMsg

	if (len < sizeof(REACPacketHeader)+sizeof(REACConstants::ENDING)) {
		fprintf(stderr, "Error: too short packet %d\n", len);
		return 1;
	}

	// packet ending
	if(memcmp(data+len-sizeof(REACConstants::ENDING), REACConstants::ENDING, sizeof(REACConstants::ENDING))) {
		fprintf(stderr, "ENDING not match: %02X %02X, expected %02X %02X\n",
				data[len-sizeof(REACConstants::ENDING)],
				data[len-sizeof(REACConstants::ENDING)+1],
				REACConstants::ENDING[0],
				REACConstants::ENDING[1] );
		return 1;
	}

	const REACPacketHeader *packetHeader = (const REACPacketHeader*)data;

	// It seems that packet with type=00:00 don't have the check-sum.
	if(packetHeader->type[0] || packetHeader->type[1]) {
		if(uint8_t s = check_checksum(packetHeader)) {
			fprintf(stderr, "Error: checksum failed sum=%02X, counter=%02X:%02X, type=%02X:%02X", s, packetHeader->counter[0], packetHeader->counter[1], packetHeader->type[0], packetHeader->type[1]);
			for(int i=0; i<32+1; i++) { fprintf(stderr, " %02X", packetHeader->data[i]); } putc('\n', stderr);
		}
		else {
			if(settings::mode==reacmode_split) {
				split_got_packet(packetHeader);
			}
		}
	}


	{
		uint16_t c = packetHeader->counter[0] | packetHeader->counter[1]<<8;
		if(counter_recv_second) {
			if(c < counter_recv_last)
				counter_recv_upper += 1;
			uint16_t skipped = (c - counter_recv_last - 1) & 0xFFFF;
			n_skipped_counter += skipped;
			if(skipped) {
				int ss = ((counter_recv_upper<<16 | c) - counter_recv_last1) / 4000;
				int mm = ss/60; ss%=60;
				int hh = mm/60; mm%=60;
				fprintf(
						stderr,
						"Error: missing %d packet(s) at %02d:%02d:%02d\n",
						skipped,
						hh, mm, ss
						);
			}
		}
		else {
			counter_recv_second = 1;
			counter_recv_last1 = c;
		}
		counter_recv_last = c;
	}

	n_samples += 12 * (1+n_skipped_counter);

	if(settings::save_mode == save_packets) {
		write_packets(data_packet, len_packet);
		return 0;
	}

	// TODO: receive audio, see copyAudioFromMbufToBuffer and txt2wavtxt.awk
	// Not only type=0000, all packet types include the audio data.
	if(settings::save_mode==save_pcm) {
		save_pcm_data_s24be(data + sizeof(REACPacketHeader), n_skipped_counter);
		n_skipped_counter = 0;
	}
	else if(settings::save_mode==save_pcm16) {
		save_pcm_data_s16be(data + sizeof(REACPacketHeader), n_skipped_counter);
		n_skipped_counter = 0;
	}
	else if(settings::save_mode==save_wave24) {
		save_pcm_data_s24le(data + sizeof(REACPacketHeader), n_skipped_counter);
		n_skipped_counter = 0;
	}
	else if(settings::save_mode==save_wave16) {
		save_pcm_data_s16le(data + sizeof(REACPacketHeader), n_skipped_counter);
		n_skipped_counter = 0;
	}
	return 0;
}

static
void split_got_packet(const REACPacketHeader *packetHeader)
{
	// REACSplitDataStream::gotPacket

	if(!(packetHeader->type[0]==0xCF && packetHeader->type[1]==0xEA)) // not REAC_STREAM_MASTER_ANNOUNCE
		return;

	switch(g_handshake_state) {
		case HANDSHAKE_NOT_INITIATED:
			if(packetHeader->data[6]==0x0D) {
				memcpy(master_addr, packetHeader->data+9, sizeof(master_addr));
				n_master_in_channels = packetHeader->data[15];
				n_master_out_channels = packetHeader->data[16];
				g_handshake_state = HANDSHAKE_GOT_MASTER_ANNOUNCE;
				fprintf(stderr, "handshake: got master announce: master_addr=%02X:%02X:%02X:%02X:%02X:%02X in-channels=%d out-channels=%d\n", master_addr[0], master_addr[1], master_addr[2], master_addr[3], master_addr[4], master_addr[5], n_master_in_channels, n_master_out_channels);
			}
			break;
		case HANDSHAKE_SENT_FIRST_ANNOUNCE:
			if(packetHeader->data[6]==0x0A && !memcmp(packetHeader->data+9, interfaceAddr, sizeof(interfaceAddr))) {
				splitIdentifier = packetHeader->data[16];
				g_handshake_state = HANDSHAKE_GOT_SECOND_MASTER_ANNOUNCE;
				fprintf(stderr, "handshake: got second announce: split identifier=%d\n", splitIdentifier);
			}
			break;
	}
}

int rr_loop(ringbuf &rb)
{
	for(;;) {
		if(!rb.has_data()) {
			usleep(250);
			continue;
		}
		ringbuf::buf_s &buf = rb.read_nextbuf();

		// work for the buf
		got_msg(buf.buf, buf.len);

		rb.read();
	}
	return 0;
}

int load_pcap_offline(FILE *fp, int (*got_msg)(const uint8_t *, int));
#ifdef _WIN32
extern int load_pcap_device_win(const char *if_name, int (*got_msg)(const uint8_t *, int));
#endif

int print_summary()
{
	// TODO: fix REAC_PACKETS_PER_SECOND
	int ms = ((counter_recv_upper<<16 | counter_recv_last) - counter_recv_last1) / 4;
	int ss = ms/1000; ms%=1000;
	int mm = ss/60; ss%=60;
	int hh = mm/60; mm%=60;
	printf("Total time: %02d:%02d:%02d.%03d\n", hh, mm, ss, ms);
	printf("%s: %d packet%s dropped.\n", n_skipped_counter?"Error":"Info", n_skipped_counter, n_skipped_counter>1?"s":"");
	return 0;
}

// Helper function to parse channel selection string like "1,2,5-8"
static int parse_channels_str(const std::string& s_channels, uint64_t& skip_mask) {
    uint64_t channel_mask = 0;
    std::string temp_s = s_channels;
    char* s = &temp_s[0];

    auto parse_channels_n_to_n = [](char* arg, uint64_t* channels) -> char* {
        char* nxt = nullptr;
        long x = strtol(arg, &nxt, 10);
        if (x <= 0 || x > 40 || nxt == arg) return nullptr;
        if (*nxt == '-') {
            long y = strtol(nxt + 1, &nxt, 10);
            if (y < x || y > 40 || nxt == arg) return nullptr;
            for (long i = x; i <= y; i++) *channels |= 1LL << (i - 1);
        } else {
            *channels |= 1LL << (x - 1);
        }
        return nxt;
    };

    while (*s) {
        char* nxt = parse_channels_n_to_n(s, &channel_mask);
        if (!nxt) return 1; // Parse error
        s = nxt;
        while (*s && !isdigit(*s)) s++;
    }
    
    skip_mask = ~channel_mask;
    return 0;
}

static char *parse_channels_n_to_n(char *arg, uint64_t *channels)
{
	char *nxt = 0;
	int x = strtol(arg, &nxt, 10);
	if(x<0 || 40<x) return NULL;
	if(nxt==arg) return NULL;
	if(*nxt=='-') {
		int y = strtol(nxt+1, &nxt, 10);
		if(y<x || 40<y) return NULL;
		if(nxt==arg) return NULL;
		for(int i=x; i<=y; i++) *channels |= 1LL << i-1;
	}
	else
		*channels |= 1LL << x-1;
	if(!*nxt || *nxt==',') return nxt;
	return 0;
}

static int parse_channels(char *arg, uint64_t *channels)
{
	*channels = 0;
	while(*arg) {
		char *nxt = parse_channels_n_to_n(arg, channels);
		if(!nxt) return __LINE__;
		if(nxt != arg) arg = nxt; else break;
		while(*arg && !isdigit(*arg)) arg++;
	}
	return 0;
}

static inline uint64_t channel_l2r(uint64_t ch) { return (ch << 1) & 0xAAAAAAAAAALL; }
static inline uint64_t channel_r2l(uint64_t ch) { return (ch >> 1) & 0x5555555555LL; }
static inline uint64_t channel_l2r_or_r2l(uint64_t ch) { return channel_l2r(ch) | channel_r2l(ch); }

static const char *help_message = "\
reaccapture - capture/postprocess REAC packets\n\
\n\
Input options:\n\
	--load-pcap-file\n\
		Load pcap format file. The file can be specified in argument.\n\
		If no file is specified, it will read from stdin.\n\
	-d if_name\n\
		Capture if_name device in promiscuous mode.\n\
	--read-stdin, -r\n\
		Read packets from stdin in the custom format (uint32_t length + packet data), e.g. from pcapreader_win.exe.\n\
	--live-win <interface>\n\
		Launch pcapreader_win.exe and read from it.\n\
	--live-bat\n\
		Use run_live_reaccapture.bat to capture for a fixed duration, then process the resulting file.\n\
Output options:\n\
	--save-wav-{16,24}\n\
		Save audio data as 16-bit or 24-bit WAVE format.\n\
	--save-pcm\n\
		Save audio data as 24-bit PCM format.\n\
	--save-pcm-16\n\
		Save audio data as 16-bit PCM format.\n\
	--save-channels arg\n\
		Limit to save channels specified by arg.\n\
	--stereo-channels arg\n\
		Couple pair(s) of adjacent channels as stereo.\n\
	--name-{1-40} file_name\n\
		Set file_name as the output file name.\n\
Other options:\n\
	-v\n\
		Increase verbose level.\n\
\n\
Version: " PACKAGE_STRING;

static FILE *fp_inp = NULL;

static int load_packets_stdin(int (*got_msg)(const uint8_t *, int));

static int load_packets_from_reader(const char *p_interface_name, packet_handler_t got_msg_func);

int parse_arg(int argc, char **argv)
{
	// This function will be removed in favor of an interactive setup.
	return 0;
}

// New helper function to list network interfaces
void list_interfaces() {
    std::string cmd = "\"C:\\Program Files\\Wireshark\\dumpcap.exe\" -D";
    std::array<char, 128> buffer;
    std::string result;
    
    std::cout << "\n--- Available Network Interfaces ---" << std::endl;
    
    FILE* pipe = _popen(cmd.c_str(), "r");
    if (!pipe) {
        std::cerr << "Error: Could not run dumpcap.exe to list interfaces." << std::endl;
        std::cerr << "Please ensure Wireshark is installed in the default location." << std::endl;
        return;
    }
    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
        std::cout << "  " << buffer.data();
    }
    _pclose(pipe);
    std::cout << "------------------------------------" << std::endl;
    std::cout << "Copy the full line for the interface you want to use (e.g., \\Device\\NPF_{...})" << std::endl;
}

int main(int argc, char **argv)
{
#ifdef _WIN32
	WSADATA wsaData;
	int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (iResult != 0) {
		fprintf(stderr, "WSAStartup failed: %d\n", iResult);
		return 1;
	}
#endif

    const char* config_filename = "reaccapture_config.txt";
    std::string save_path, s_duration, s_channels, s_interface;
    int duration = 10;

    auto save_settings = [&]() {
        std::ofstream config_out(config_filename);
        config_out << save_path << std::endl;
        config_out << duration << std::endl;
        config_out << s_channels << std::endl;
        config_out << s_interface << std::endl;
        config_out.close();
    };
    
    // Load settings
    std::ifstream config_in(config_filename);
    if (config_in.is_open()) {
        std::getline(config_in, save_path);
        if (std::getline(config_in, s_duration) && !s_duration.empty()) duration = std::stoi(s_duration);
        std::getline(config_in, s_channels);
        std::getline(config_in, s_interface);
        config_in.close();
    }

    // --- First time setup prompts ---
    bool settings_changed = false;
    if (save_path.empty()) {
        std::cout << "--- First-Time Setup ---" << std::endl;
        std::cout << "Enter the full path where recordings will be saved: ";
        std::getline(std::cin, save_path);
        settings_changed = true;
    }
    if (s_duration.empty()) {
        std::cout << "Enter default capture duration in seconds (e.g., 10): ";
        std::string input;
        std::getline(std::cin, input);
        if (!input.empty()) duration = std::stoi(input); else s_duration = std::to_string(duration);
        settings_changed = true;
    }
    if (s_channels.empty()) {
        std::cout << "Enter channels to record (e.g., 1-8,12,15 or 'all'): ";
        std::getline(std::cin, s_channels);
        if (s_channels.empty() || s_channels == "all") s_channels = "1-40";
        settings_changed = true;
    }
    if (s_interface.empty()) {
        list_interfaces();
        std::cout << "\nEnter the network interface GUID to use for capture: ";
        std::getline(std::cin, s_interface);
        settings_changed = true;
    }
    if (settings_changed) {
        save_settings();
        std::cout << "\nSettings saved. Starting application." << std::endl;
    }


    // --- Interactive Loop ---
    while (true) {
        std::cout << "\n--- REAC Live Capture ---" << std::endl;
        std::cout << "  1. Save Path:    " << save_path << std::endl;
        std::cout << "  2. Duration (s): " << duration << std::endl;
        std::cout << "  3. Channels:     " << s_channels << std::endl;
        std::cout << "  4. Interface:    " << s_interface << std::endl;
        std::cout << "\nOptions: (r)ecord | (p)ath | (d)uration | (c)hannels | (i)nterface | (q)uit" << std::endl;
        std::cout << "Enter your choice: ";
        
        char choice;
        std::cin >> choice;
        std::cin.ignore(10000, '\n'); 

        if (choice == 'q' || choice == 'Q') {
            break;
        } else if (choice == 'p' || choice == 'P') {
            std::cout << "Enter the new full path to save recordings: ";
            std::getline(std::cin, save_path);
            if (save_path.empty()) std::cout << "Invalid path. Path not changed." << std::endl;
            else { save_settings(); std::cout << "Save path updated." << std::endl; }
            continue;
        } else if (choice == 'd' || choice == 'D') {
            std::cout << "Enter new capture duration in seconds: ";
            std::string input;
            std::getline(std::cin, input);
            try { 
                if (!input.empty()) duration = std::stoi(input);
                save_settings();
                std::cout << "Duration updated." << std::endl;
            } catch(...) { std::cout << "Invalid number. Duration not changed." << std::endl; }
            continue;
        } else if (choice == 'c' || choice == 'C') {
            std::cout << "Enter channels to record (e.g., 1-8,12 or 'all'): ";
            std::getline(std::cin, s_channels);
            if (s_channels.empty()) std::cout << "Invalid input. Channels not changed." << std::endl;
            else {
                if(s_channels == "all") s_channels = "1-40";
                save_settings();
                std::cout << "Channel selection updated." << std::endl;
            }
            continue;
        } else if (choice == 'i' || choice == 'I') {
            list_interfaces();
            std::cout << "Enter the new network interface GUID (or press Enter to cancel): ";
            std::string input;
            std::getline(std::cin, input);
            if (!input.empty()) {
                s_interface = input;
                save_settings();
                std::cout << "Interface updated." << std::endl;
            } else {
                std::cout << "Interface not changed." << std::endl;
            }
            continue;
        }
        else if (choice != 'r' && choice != 'R') {
            std::cout << "Invalid option. Please try again." << std::endl;
            continue;
        }

        // --- Execute Capture (if 'r' was chosen) ---
        settings::save_mode = save_wave24;
        if (parse_channels_str(s_channels, settings::skip_channel) != 0) {
            fprintf(stderr, "Error: Invalid channel string '%s'. Aborting capture.\n", s_channels.c_str());
            continue;
        }

        // Reset counters for new recording
        n_samples = 0;
        n_skipped_counter = 0;
        counter_recv_last=0;
        counter_recv_upper=0;
        counter_recv_second = 0;

        save_wav_init();

        char batch_command[1024];
        snprintf(batch_command, sizeof(batch_command), "run_live_reaccapture.bat %d \"%s\"", duration, s_interface.c_str());
        
        const char* temp_filename = "live_capture_temp.pcap";
        fprintf(stderr, "\nLaunching live capture for %d seconds. Please wait...\n", duration);
        int result = system(batch_command);

        if (result != 0) {
            fprintf(stderr, "Error: Batch script failed. It may have been interrupted or could not find dumpcap.exe.\n");
            continue; 
        }

        FILE* fp_temp = fopen(temp_filename, "rb");
        if (!fp_temp) {
            fprintf(stderr, "Error: Could not open temporary capture file '%s'. Capture may have failed.\n", temp_filename);
            continue;
        }
        
        fprintf(stderr, "Processing captured data...\n");
        int ret = load_pcap_offline(fp_temp, got_msg) | print_summary();
        fclose(fp_temp);
        remove(temp_filename); 

        if(settings::save_mode==save_wave24 || settings::save_mode==save_wave16)
            save_wav_fin();

        // --- Organize Files ---
        time_t now = time(0);
        struct tm tstruct;
        char time_buf[80];
        tstruct = *localtime(&now);
        strftime(time_buf, sizeof(time_buf), "%Y-%m-%d_%H-%M-%S", &tstruct);

        std::string timestamp_dir = save_path + "/" + time_buf;
        if (MKDIR(timestamp_dir.c_str()) != 0) {
            perror("Error creating timestamped directory");
        } else {
            std::cout << "Created directory: " << timestamp_dir << std::endl;
            for (int i = 0; i < 40; ++i) {
                if(settings::skip_channel & (1LL<<i)) continue;

                char original_filename[64];
                snprintf(original_filename, sizeof(original_filename), "output-%02d.wav", i + 1);
                
                FILE* f_check = fopen(original_filename, "rb");
                if (f_check) {
                    fclose(f_check);
                    std::string new_filepath = timestamp_dir + "/" + original_filename;
                    if (rename(original_filename, new_filepath.c_str()) != 0) {
                        perror("Error moving file");
                    } else {
                        // std::cout << "Moved " << original_filename << std::endl; // Too verbose
                    }
                }
            }
            std::cout << "Moved WAV files to " << timestamp_dir << std::endl;
        }
    } // End of while loop

#ifdef _WIN32
	WSACleanup();
#endif
	return 0;
}

static int load_packets_stdin(int (*got_msg)(const uint8_t *, int)) {
	uint32_t len;
	while (fread(&len, sizeof(len), 1, stdin) == 1) {
		if (len == 0 || len > 65536) break;
		uint8_t buf[len];
		if (fread(buf, 1, len, stdin) != len) break;
		got_msg(buf, len);
	}
	return 0;
}

static int load_packets_from_reader(const char *p_interface_name, packet_handler_t got_msg_func) {
#ifdef _WIN32
	char cmd[512];
	snprintf(cmd, sizeof(cmd), "pcapreader_win.exe \"%s\"", p_interface_name);
	FILE *fp = _popen(cmd, "rb");
	if (!fp) {
		fprintf(stderr, "Failed to launch pcapreader_win.exe\n");
		return 1;
	}
	uint32_t len;
	while (fread(&len, sizeof(len), 1, fp) == 1) {
		if (len == 0 || len > 65536) break;
		uint8_t buf[len];
		if (fread(buf, 1, len, fp) != len) break;
		got_msg_func(buf, len);
	}
	_pclose(fp);
	return 0;
#else
	fprintf(stderr, "Live reader mode is only supported on Windows.\n");
	return 1;
#endif
}
