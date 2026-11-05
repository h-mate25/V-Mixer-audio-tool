#include "reac/protocol.hpp"
#include <algorithm>
#include <numeric>
#include <stdexcept>

namespace reac {
namespace {
std::uint16_t be16(std::span<const std::uint8_t> b, std::size_t p) {
    return static_cast<std::uint16_t>((unsigned(b[p]) << 8U) | b[p + 1]);
}
Control inspect_control(std::uint16_t type, std::span<const std::uint8_t> b,
                        const std::array<std::uint8_t, 6>& source) {
    Control c;
    if (type == 0) {
        c.kind = "filler"; c.outer_checksum = "exempt"; c.bounds = "not_applicable";
        return c;
    }
    c.outer_checksum = (std::accumulate(b.begin(), b.end(), 0U) % 256 == 0) ? "pass" : "fail";
    c.valid = c.outer_checksum == "pass";
    c.op = be16(b, 0); c.record_length = be16(b, 2);
    if (type != 0xcdea && type != 0xcfea) {
        c.kind = "unknown_type"; c.bounds = "unknown"; return c;
    }
    if (type == 0xcfea) {
        // An all-zero block also has a good checksum. Require the known carrier,
        // fixed header, plausible widths, and embedded source before reading rate.
        constexpr std::array<std::uint8_t, 9> prefix{0xff,0xff,1,0,1,3,0x0d,1,4};
        const bool carrier = std::equal(prefix.begin(), prefix.end(), b.begin()) &&
            std::equal(source.begin(), source.end(), b.begin()+9) &&
            b[15] >= 2 && b[15] <= 40 && b[15] % 2 == 0 && b[16] <= 40;
        c.kind = carrier ? "announcement" : "unrecognized_announcement";
        c.bounds = carrier ? "known_fixed_carrier" : "unknown";
        if (carrier && c.valid && b[17] <= 2) {
            c.rate_class = b[17];
            constexpr unsigned rates[]{48000,96000,44100};
            c.rate_hz = rates[b[17]];
        }
        return c;
    }
    c.kind = "control_record";
    // Link-1 subtype-zero bulk records count from their fragment payload,
    // unlike generic records, which count from block[4]. No reassembly in M1.
    const bool bulk = b[0] == 1 && b[4] == 0;
    const std::size_t base = bulk ? ((b[1] & 1U) ? 7U : 5U) : 4U;
    const bool bounds = *c.record_length > 0 && *c.record_length <= 31 - base;
    c.bounds = bounds ? "pass" : "fail";
    c.valid = c.valid && bounds;
    if (b[0] != 4) return c;
    if (b[1] != 3) { c.dt1 = "fragment_not_reassembled"; return c; }
    constexpr std::array<std::uint8_t,4> wrapper{0,2,0,0xfe};
    if (!std::equal(wrapper.begin(), wrapper.end(), b.begin()+4) || b[9]!=0xf0 || b[10]!=0x41) {
        c.dt1 = "unrecognized_container"; return c;
    }
    c.lengths_match = *c.record_length == unsigned(b[8]) + 5U;
    // Trust neither outer length nor echo for unchecked indexing. SysEx must fit
    // before block[31], with four address bytes and an inner checksum.
    if (b[8] < 13 || b[8] > 22) {
        c.dt1 = "invalid_sysex_length"; c.valid = false; return c;
    }
    const std::size_t end = 9U + b[8] - 1U;
    c.terminator_ok = b[end] == 0xf7;
    c.inner_checksum_ok = (std::accumulate(b.begin()+16, b.begin()+static_cast<std::ptrdiff_t>(end), 0U) % 128U) == 0;
    c.seven_bit_ok = std::all_of(b.begin()+11, b.begin()+static_cast<std::ptrdiff_t>(end),
                                [](auto v){return v < 128;});
    c.padding_ok = std::all_of(b.begin()+static_cast<std::ptrdiff_t>(end+1), b.begin()+31,
                              [](auto v){return v == 0;});
    c.address = (std::uint32_t(b[16])<<24U) | (std::uint32_t(b[17])<<16U) |
                (std::uint32_t(b[18])<<8U) | b[19];
    const bool model = b[12]==0 && b[13]==0 && b[14]==0x12 && b[15]==0x12;
    const bool integrity = bounds && *c.lengths_match && *c.terminator_ok &&
                           *c.inner_checksum_ok && *c.seven_bit_ok && *c.padding_ok;
    c.valid = c.valid && integrity;
    if (!integrity) { c.dt1 = "invalid"; return c; }
    if (!model) { c.dt1 = "unsupported_model_or_command"; return c; }
    c.dt1 = "complete_dt1";
    const auto page = *c.address >> 16U;
    if (page==0x0101 && b[8]==14) c.semantic="head_amp";
    else if (*c.address==0x01000600 && b[8]==15) c.semantic="join_record";
    else if (*c.address==0x00000300 && b[8]==15) c.semantic="head_mark";
    else if (*c.address==0x03020001 && b[8]==14) c.semantic="box_ready_record";
    else if (page==0x0500) c.semantic="identity_record";
    return c;
}
}
bool supported(Geometry g) {
    return (g.samples==12 && g.channels>=2 && g.channels<=40 && g.channels%2==0) ||
           (g.samples==6 && g.channels==16);
}
std::vector<std::int32_t> decode_pcm(std::span<const std::uint8_t> audio, Geometry g) {
    if (!supported(g) || audio.size()!=3*g.channels*g.samples)
        throw std::invalid_argument("unsupported geometry or incorrect audio size");
    std::vector<std::int32_t> out(g.channels*g.samples);
    for (std::size_t t=0; t<g.samples; ++t) for (std::size_t ch=0; ch<g.channels; ++ch) {
        const auto p=(t*g.channels+(ch & ~std::size_t{1}))*3;
        const std::uint32_t value = ch%2==0 ?
            unsigned(audio[p+3]) | (unsigned(audio[p])<<8U) | (unsigned(audio[p+1])<<16U) :
            unsigned(audio[p+4]) | (unsigned(audio[p+5])<<8U) | (unsigned(audio[p+2])<<16U);
        out[ch*g.samples+t] = value & 0x800000U ? static_cast<std::int32_t>(value)-0x1000000 : static_cast<std::int32_t>(value);
    }
    return out;
}
std::vector<std::uint8_t> encode_pcm(std::span<const std::int32_t> pcm, Geometry g) {
    if (!supported(g) || pcm.size()!=g.channels*g.samples)
        throw std::invalid_argument("unsupported geometry or incorrect sample count");
    std::vector<std::uint8_t> out(pcm.size()*3);
    for (std::size_t t=0; t<g.samples; ++t) for (std::size_t ch=0; ch<g.channels; ++ch) {
        const auto sample=pcm[ch*g.samples+t];
        if (sample < -8388608 || sample > 8388607) throw std::invalid_argument("sample outside s24 range");
        const auto v=static_cast<std::uint32_t>(sample)&0xffffffU;
        const auto p=(t*g.channels+(ch & ~std::size_t{1}))*3;
        if (ch%2==0) {out[p+3]=static_cast<std::uint8_t>(v); out[p]=static_cast<std::uint8_t>(v>>8U); out[p+1]=static_cast<std::uint8_t>(v>>16U);}
        else {out[p+4]=static_cast<std::uint8_t>(v); out[p+5]=static_cast<std::uint8_t>(v>>8U); out[p+2]=static_cast<std::uint8_t>(v>>16U);}
    }
    return out;
}
Inspection inspect(std::span<const std::uint8_t> b, Options opt) {
    Inspection r; r.raw_length=b.size();
    auto fail=[&](std::string message){ r.diagnostics.push_back(std::move(message)); return r; };
    if (opt.residue_bytes!=0 && opt.residue_bytes!=2) return fail("unsupported_residue_policy");
    if (b.size()<14) return fail("truncated_ethernet_header");
    std::copy_n(b.begin(),6,r.destination.begin()); std::copy_n(b.begin()+6,6,r.source.begin());
    std::size_t shift=0;
    auto eth=be16(b,12);
    if (eth==0x8100) {
        if (b.size()<18) return fail("truncated_vlan_header");
        r.vlan_tci=be16(b,14); shift=4; eth=be16(b,16);
        if (eth==0x8100 || eth==0x88a8) return fail("unsupported_stacked_vlan");
    }
    if (eth==0x88a8 || eth==0x9100) return fail("unsupported_vlan_tpid");
    if (eth!=0x8819) return fail("unsupported_ethertype_or_damaged_header");
    if (b.size()<52+shift+opt.residue_bytes) return fail("truncated_reac_header_or_tail");
    r.clean_length=b.size()-opt.residue_bytes;
    r.sequence=static_cast<std::uint16_t>(unsigned(b[14+shift]) | (unsigned(b[15+shift])<<8U));
    r.type=be16(b,16+shift);
    if (b[r.clean_length-2]!=0xc2 || b[r.clean_length-1]!=0xea) return fail("bad_end_marker_or_undeclared_trailer");
    const auto audio_size=r.clean_length-52-shift;
    if (audio_size==0 || audio_size%6!=0) return fail("invalid_audio_bounds");
    if (opt.geometry) {
        if (!supported(*opt.geometry)) return fail("unsupported_geometry");
        if (audio_size!=3*opt.geometry->channels*opt.geometry->samples) return fail("geometry_length_mismatch");
        r.geometry=opt.geometry;
    }
    r.envelope_valid=true;
    r.control=inspect_control(*r.type,b.subspan(18+shift,32),r.source);
    if (!r.geometry) {
        r.status=audio_size==288 ? "ambiguous" : "geometry_required";
        r.diagnostics.push_back(audio_size==288 ? "requires_8x12_or_diagnostic_16x6_context" : "explicit_geometry_required");
        return r;
    }
    r.diagnostic_geometry=r.geometry->samples==6;
    if (r.diagnostic_geometry) r.diagnostics.push_back("16x6_diagnostic_only_hardware_acceptance_unproved");
    if (opt.residue_bytes) r.diagnostics.push_back("two_capture_residue_bytes_preserved_not_validated_as_fcs");
    r.pcm=decode_pcm(b.subspan(50+shift,audio_size),*r.geometry);
    r.status="valid";
    if (!r.control.valid) r.diagnostics.push_back("control_integrity_or_length_failure_pcm_retained");
    if (*r.type!=0 && *r.type!=0xcdea && *r.type!=0xcfea) r.diagnostics.push_back("unknown_frame_type_pcm_decoded_under_explicit_geometry");
    return r;
}
} // namespace reac
