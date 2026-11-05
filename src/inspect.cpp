#include "reac/protocol.hpp"
#include <charconv>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace {
unsigned number(std::string_view s) {
    unsigned n=0; auto [p,e]=std::from_chars(s.data(),s.data()+s.size(),n);
    if (e!=std::errc{} || p!=s.data()+s.size()) throw std::invalid_argument("invalid unsigned integer");
    return n;
}
reac::Geometry geometry(std::string_view s) {
    const auto x=s.find('x');
    if (x==s.npos) throw std::invalid_argument("geometry must be CHANNELSxSAMPLES");
    return {number(s.substr(0,x)),number(s.substr(x+1))};
}
std::vector<std::uint8_t> hex(std::string_view s) {
    std::vector<std::uint8_t> out; int high=-1;
    for (unsigned char ch : s) {
        if (std::isspace(ch)) continue;
        int v=ch>='0'&&ch<='9'? ch-'0' : ch>='a'&&ch<='f'?ch-'a'+10:ch>='A'&&ch<='F'?ch-'A'+10:-1;
        if (v<0) throw std::invalid_argument("non-hexadecimal input");
        if (high<0) high=v; else {out.push_back(static_cast<std::uint8_t>(high*16+v)); high=-1;}
        if (out.size()>65536) throw std::invalid_argument("frame exceeds 65536-byte input limit");
    }
    if (high>=0) throw std::invalid_argument("odd hex digit count");
    return out;
}
std::string mac(const std::array<std::uint8_t,6>& b) {
    std::ostringstream s; s<<std::hex<<std::setfill('0');
    for(std::size_t i=0;i<b.size();++i) {if(i) s<<':'; s<<std::setw(2)<<unsigned(b[i]);}
    return s.str();
}
template<class T> void optional(const std::optional<T>& value) {
    if(value) std::cout<<+*value; else std::cout<<"null";
}
void boolean(const std::optional<bool>& value) {
    if(value) std::cout<<(*value?"true":"false"); else std::cout<<"null";
}
void json(const reac::Inspection& r,std::size_t index,bool pcm) {
    const auto& c=r.control;
    std::cout<<"{\"record\":"<<index<<",\"status\":\""<<r.status<<"\",\"envelope_valid\":"<<(r.envelope_valid?"true":"false")
             <<",\"raw_length\":"<<r.raw_length<<",\"clean_length\":"<<r.clean_length
             <<",\"source\":\""<<mac(r.source)<<"\",\"destination\":\""<<mac(r.destination)<<"\",\"vlan_tci\":";
    optional(r.vlan_tci); std::cout<<",\"sequence\":"; optional(r.sequence);
    std::cout<<",\"type\":"; optional(r.type);
    std::cout<<",\"width\":"; if(r.geometry) std::cout<<r.geometry->channels; else std::cout<<"null";
    std::cout<<",\"samples_per_packet\":"; if(r.geometry) std::cout<<r.geometry->samples; else std::cout<<"null";
    std::cout<<",\"diagnostic_geometry\":"<<(r.diagnostic_geometry?"true":"false")
             <<",\"decoded_samples\":"<<r.pcm.size()<<",\"control\":{\"kind\":\""<<c.kind
             <<"\",\"valid\":"<<(c.valid?"true":"false")<<",\"outer_checksum\":\""<<c.outer_checksum
             <<"\",\"bounds\":\""<<c.bounds<<"\",\"op\":"; optional(c.op);
    std::cout<<",\"record_length\":"; optional(c.record_length);
    std::cout<<",\"dt1\":\""<<c.dt1<<"\",\"semantic\":\""<<c.semantic<<"\",\"address\":"; optional(c.address);
    std::cout<<",\"lengths_match\":"; boolean(c.lengths_match);
    std::cout<<",\"terminator_ok\":"; boolean(c.terminator_ok);
    std::cout<<",\"inner_checksum_ok\":"; boolean(c.inner_checksum_ok);
    std::cout<<",\"seven_bit_ok\":"; boolean(c.seven_bit_ok);
    std::cout<<",\"padding_ok\":"; boolean(c.padding_ok);
    std::cout<<"},\"rate_class\":"; optional(c.rate_class);
    std::cout<<",\"announced_rate_hz\":"; optional(c.rate_hz);
    std::cout<<",\"diagnostics\":[";
    for(std::size_t i=0;i<r.diagnostics.size();++i) {if(i) std::cout<<','; std::cout<<'"'<<r.diagnostics[i]<<'"';}
    std::cout<<']';
    if(pcm) {std::cout<<",\"pcm_channel_major\":["; for(std::size_t i=0;i<r.pcm.size();++i) {if(i) std::cout<<',';std::cout<<r.pcm[i];} std::cout<<']';}
    std::cout<<"}\n";
}
void usage() {
    std::cout<<"reac-inspect (--frame HEX | --hex-file PATH | --binary PATH)\n"
             <<"             [--geometry CHANNELSxSAMPLES] [--residue 0|2] [--pcm]\n"
             <<"Hex file: one complete Ethernet frame per line; blank/# lines ignored.\n"
             <<"Output: JSON Lines; PCM is signed s24 in channel-major order.\n"
             <<"FCS must be absent. A single inline 802.1Q tag is supported.\n"
             <<"Exit 0: decoded, 1: invalid/ambiguous/geometry missing, 2: input error.\n";
}
}
int main(int argc,char** argv) {
    try {
        reac::Options opt; std::string mode,input; bool pcm=false;
        for(int i=1;i<argc;++i) {
            const std::string_view a=argv[i];
            if(a=="--help") {usage();return 0;}
            if(a=="--pcm") {pcm=true;continue;}
            if(i+1>=argc) throw std::invalid_argument("option requires a value");
            const std::string value=argv[++i];
            if(a=="--geometry") opt.geometry=geometry(value);
            else if(a=="--residue") opt.residue_bytes=number(value);
            else if(a=="--frame" || a=="--hex-file" || a=="--binary") {
                if(!mode.empty()) throw std::invalid_argument("choose one input mode");
                mode=a;input=value;
            } else throw std::invalid_argument("unknown option");
        }
        if(mode.empty()) {usage();return 2;}
        int status=0; std::size_t count=0;
        auto run=[&](const std::vector<std::uint8_t>& bytes) {
            const auto r=reac::inspect(bytes,opt); json(r,++count,pcm);
            if(r.status!="valid" || !r.control.valid) status=1;
        };
        if(mode=="--frame") run(hex(input));
        else {
            std::ifstream f(input,std::ios::binary);
            if(!f) throw std::runtime_error("cannot open input file");
            if(mode=="--binary") {
                std::vector<std::uint8_t> b; char ch;
                while(f.get(ch)) {b.push_back(static_cast<std::uint8_t>(ch));if(b.size()>65536) throw std::runtime_error("binary frame too large");}
                run(b);
            } else {
                // Bound each line even on hostile input; do not allocate an entire capture.
                std::string line; char ch;
                auto flush=[&] {
                    const auto first=line.find_first_not_of(" \t\r");
                    if(first!=line.npos && line[first]!='#') run(hex(line));
                    line.clear();
                };
                while(f.get(ch)) {if(ch=='\n') flush(); else {line+=ch;if(line.size()>262144) throw std::runtime_error("hex line too large");}}
                flush();
            }
            if(f.bad()) throw std::runtime_error("input read failure");
        }
        if(count==0) throw std::runtime_error("no frame records");
        return status;
    } catch(const std::exception& e) {std::cerr<<"reac-inspect: "<<e.what()<<'\n';return 2;}
}
