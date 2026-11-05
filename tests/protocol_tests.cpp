#include "reac/protocol.hpp"
#include <algorithm>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
void check(bool ok,const char* what) {if(!ok) throw std::runtime_error(what);}
template<class F> void rejects(F f) {bool caught=false;try {f();} catch(const std::invalid_argument&) {caught=true;} check(caught,"expected invalid_argument");}
std::vector<std::uint8_t> frame(reac::Geometry g={8,12}) {
    std::vector<std::uint8_t> b(52+3*g.channels*g.samples);
    std::fill_n(b.begin(),6,0xff); b[6]=2;b[11]=1;b[12]=0x88;b[13]=0x19;
    b[14]=0xfe;b[15]=0xca; b[b.size()-2]=0xc2;b.back()=0xea;return b;
}
void stamp(std::vector<std::uint8_t>& f) {
    unsigned sum=0;for(std::size_t i=18;i<49;++i) sum+=f[i];f[49]=static_cast<std::uint8_t>(0U-sum);
}
void dt1(std::vector<std::uint8_t>& b) {
    // Authored SINGLE DT1: address 01 01 00 00, value 01, inner sum 128.
    const std::vector<std::uint8_t> block{4,3,0,19,0,2,0,0xfe,14,0xf0,0x41,0x0a,0,0,0x12,0x12,1,1,0,0,1,0x7d,0xf7};
    b[16]=0xcd;b[17]=0xea;std::copy(block.begin(),block.end(),b.begin()+18);stamp(b);
}
void codec() {
    for(auto g:{reac::Geometry{2,12},{8,12},{16,12},{24,12},{32,12},{40,12},{16,6}}) {
        const std::int32_t edges[]{-8388608,-1,0,1,8388607,0x123456};
        std::vector<std::int32_t> p(g.channels*g.samples);
        for(std::size_t i=0;i<p.size();++i)p[i]=edges[i%6];
        const auto a=reac::encode_pcm(p,g);check(reac::decode_pcm(a,g)==p,"signed edge round trip");
        p[0]=8388608;rejects([&]{reac::encode_pcm(p,g);});
        p[0]=-8388609;rejects([&]{reac::encode_pcm(p,g);});
    }
    rejects([]{reac::decode_pcm({}, {3,12});});
    rejects([]{reac::decode_pcm({}, {40,12});});
    rejects([]{reac::encode_pcm({}, {40,12});});
}
void ordering() {
    // Independent literal pair bytes: even 0x123456, odd -8388607 (0x800001).
    const std::vector<std::uint8_t> pair{0x34,0x12,0x80,0x56,0x01,0x00};
    std::vector<std::uint8_t> bytes(72);
    std::copy(pair.begin(),pair.end(),bytes.begin());
    auto p=reac::decode_pcm(bytes,{2,12});check(p[0]==0x123456 && p[12]==-8388607,"literal pair permutation");
    // Direct wire construction independent of encode_pcm, all banks/times unique.
    const reac::Geometry g{40,12};bytes.resize(3*g.channels*g.samples);
    for(std::size_t t=0;t<12;++t) for(std::size_t ch=0;ch<40;ch+=2) {
        const auto k=(t*40+ch)*3;
        bytes[k]=static_cast<std::uint8_t>(ch); bytes[k+1]=0x01;bytes[k+2]=0xfe;
        bytes[k+3]=static_cast<std::uint8_t>(t);bytes[k+4]=static_cast<std::uint8_t>(t+32);bytes[k+5]=static_cast<std::uint8_t>(ch+1);
    }
    p=reac::decode_pcm(bytes,g);
    for(std::size_t ch=0;ch<40;++ch) for(std::size_t t=0;t<12;++t) {
        const auto expected=ch%2==0 ? 65536+static_cast<int>(ch*256+t) : -131072+static_cast<int>(ch*256+t+32);
        check(p[ch*12+t]==expected,"bank or time ordering");
    }
    check(reac::encode_pcm(p,g)==bytes,"encoder matches independent byte oracle");
    // Literal signed limits, independent of the encoder and sign extension code.
    bytes.assign(72,0);
    const std::vector<std::uint8_t> limits{0,0x80,0x7f,0,0xff,0xff, 0xff,0xff,0,0xff,0,0};
    std::copy(limits.begin(),limits.end(),bytes.begin());
    p=reac::decode_pcm(bytes,{2,12});
    check(p[0]==-8388608 && p[12]==8388607 && p[1]==-1 && p[13]==0,"literal signed limits");
}
void ambiguity() {
    auto b=frame();
    auto r=reac::inspect(b);check(r.status=="ambiguous" && r.pcm.empty(),"340 must be ambiguous");
    // Unique wire positions: the two interpretations must actually differ.
    for(std::size_t i=50;i<b.size()-2;++i)b[i]=static_cast<std::uint8_t>(i);
    const auto a=reac::inspect(b,{{{8,12}},0});
    const auto d=reac::inspect(b,{{{16,6}},0});
    check(a.status=="valid" && d.status=="valid" && d.diagnostic_geometry,"explicit geometries");
    check(a.pcm!=d.pcm && a.pcm.size()==96 && d.pcm.size()==96,"different interpretations");
    check(reac::inspect(frame({40,12})).status=="geometry_required","no implicit twelve sample assumption");
    check(!reac::inspect(b,{{{16,12}},0}).envelope_valid,"geometry bounds");
}
void envelope() {
    const reac::Options opt{{{8,12}},0};const auto original=frame();
    for(std::size_t n=0;n<52;++n) {auto b=original;b.resize(n);check(!reac::inspect(b,opt).envelope_valid,"short header accepted");}
    auto b=original;b.back()=0;check(!reac::inspect(b,opt).envelope_valid,"bad marker");
    b=original;b.erase(b.begin(),b.begin()+2);check(!reac::inspect(b,opt).envelope_valid,"missing header bytes repaired");
    b=original;b[12]=8;check(!reac::inspect(b,opt).envelope_valid,"wrong EtherType");
    b=original;b.insert(b.end()-2,0);check(!reac::inspect(b,opt).envelope_valid,"nonintegral audio bounds");
    b=original;b.insert(b.begin()+12,{0x81,0,0,7});
    auto r=reac::inspect(b,opt);check(r.status=="valid" && r.vlan_tci==7 && r.sequence==0xcafe,"inline VLAN offsets");
    b.insert(b.begin()+12,{0x81,0,0,8});check(!reac::inspect(b,opt).envelope_valid,"stacked tag");
    b=original;b[12]=0x88;b[13]=0xa8;check(!reac::inspect(b,opt).envelope_valid,"unsupported S tag");
    b=original;b.push_back(0x12);b.push_back(0x34);
    check(!reac::inspect(b,opt).envelope_valid,"undeclared residue");
    check(reac::inspect(b,{{{8,12}},2}).status=="valid","explicit two-byte residue");
    check(!reac::inspect(b,{{{8,12}},4}).envelope_valid,"unsupported FCS policy");
    b=original;auto copy=b;r=reac::inspect(b,opt);check(b==copy,"raw input changed");
}
void control() {
    const reac::Options opt{{{8,12}},0};auto b=frame();dt1(b);
    auto r=reac::inspect(b,opt);
    check(r.control.dt1=="complete_dt1" && r.control.semantic=="head_amp" && r.control.valid,"valid DT1");
    check(r.control.lengths_match==true && r.control.inner_checksum_ok==true && r.control.terminator_ok==true,"independent checks");
    auto bad=b;bad[21]=18;stamp(bad);r=reac::inspect(bad,opt);
    check(r.control.outer_checksum=="pass" && r.control.lengths_match==false && r.control.inner_checksum_ok==true && !r.control.valid,"length-inconsistent checksum-valid DT1");
    bad=b;bad[40]=0;stamp(bad);r=reac::inspect(bad,opt);
    check(r.control.terminator_ok==false && r.control.inner_checksum_ok==true,"bad terminator independent of checksum");
    bad=b;bad[38]=2;stamp(bad);r=reac::inspect(bad,opt);
    check(r.control.inner_checksum_ok==false && r.control.outer_checksum=="pass","bad inner checksum");
    bad=b;bad[39]^=0x80;stamp(bad);r=reac::inspect(bad,opt);
    check(r.control.inner_checksum_ok==true && r.control.seven_bit_ok==false && !r.control.valid,"seven-bit checksum byte required independently");
    bad=b;bad[45]=1;stamp(bad);r=reac::inspect(bad,opt);
    check(r.control.padding_ok==false && r.control.inner_checksum_ok==true && !r.control.valid,"nonzero DT1 padding");
    bad=b;bad[49]^=1;r=reac::inspect(bad,opt);
    check(r.control.outer_checksum=="fail" && r.control.inner_checksum_ok==true,"bad outer checksum");
    bad=b;bad[26]=255;stamp(bad);r=reac::inspect(bad,opt);check(r.control.dt1=="invalid_sysex_length","echo bounds");
    bad=b;bad[21]=255;stamp(bad);r=reac::inspect(bad,opt);check(r.control.bounds=="fail","outer bounds");
    bad=b;bad[18]=1;stamp(bad);r=reac::inspect(bad,opt);check(r.control.dt1=="not_applicable","embedded DT1 cannot bypass outer class");
    bad=b;bad[19]=1;stamp(bad);check(reac::inspect(bad,opt).control.dt1=="fragment_not_reassembled","fragment must not be complete");
    b=frame();b[16]=0xcf;b[17]=0xea;stamp(b);r=reac::inspect(b,opt);
    check(!r.control.rate_class && r.control.outer_checksum=="pass","empty CFEA has no rate");
    const std::vector<std::uint8_t> prefix{0xff,0xff,1,0,1,3,13,1,4};std::copy(prefix.begin(),prefix.end(),b.begin()+18);
    std::copy_n(b.begin()+6,6,b.begin()+27);b[33]=40;b[34]=16;
    for(unsigned rate=0;rate<3;++rate) {b[35]=static_cast<std::uint8_t>(rate);stamp(b);r=reac::inspect(b,opt);check(r.control.rate_class==rate,"recognized CFEA rate");}
    b[35]=0;stamp(b);b[49]^=1;check(!reac::inspect(b,opt).control.rate_class,"bad outer checksum cannot announce rate");
    b[35]=3;stamp(b);check(!reac::inspect(b,opt).control.rate_class,"unknown rate code");
    b[35]=0;b[27]^=1;stamp(b);check(!reac::inspect(b,opt).control.rate_class,"peer echo mismatch");
    b=frame();std::vector<std::int32_t> pcm(96);for(std::size_t i=0;i<pcm.size();++i)pcm[i]=static_cast<std::int32_t>(i+1);
    auto audio=reac::encode_pcm(pcm,{8,12});std::copy(audio.begin(),audio.end(),b.begin()+50);dt1(b);
    check(reac::inspect(b,opt).pcm==pcm,"control-bearing PCM retained");
    b[49]^=1;check(reac::inspect(b,opt).pcm==pcm,"damaged control must retain valid PCM");
    b[16]=0x12;b[17]=0x34;check(reac::inspect(b,opt).control.kind=="unknown_type","unknown type diagnosed");
}
void robustness() {
    // Deterministic hostile byte inputs and every truncation; useful under ASan/UBSan.
    std::uint32_t state=1234567;
    for(std::size_t n=0;n<1800;++n) {
        std::vector<std::uint8_t> b(n);
        for(auto& v:b) {state=1664525*state+1013904223;v=static_cast<std::uint8_t>(state>>24U);}
        reac::inspect(b);reac::inspect(b,{{{40,12}},0});
    }
    const auto full=frame({40,12});
    for(std::size_t n=0;n<full.size();++n) reac::inspect(std::span(full).first(n),{{{40,12}},0});
    auto b=frame();dt1(b);
    for(unsigned echo=0;echo<256;++echo) for(unsigned len:{0U,1U,19U,27U,28U,255U,65535U}) {
        b[26]=static_cast<std::uint8_t>(echo);b[20]=static_cast<std::uint8_t>(len>>8U);b[21]=static_cast<std::uint8_t>(len);stamp(b);reac::inspect(b,{{{8,12}},0});
    }
}
}
int main(int argc,char** argv) {
    try {
        if(argc!=2) throw std::runtime_error("expected case name");
        const std::string name=argv[1];
        if(name=="codec")codec();else if(name=="ordering")ordering();else if(name=="ambiguity")ambiguity();
        else if(name=="envelope")envelope();else if(name=="control")control();else if(name=="robustness")robustness();
        else throw std::runtime_error("unknown case");
        std::cout<<name<<": passed\n";return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
