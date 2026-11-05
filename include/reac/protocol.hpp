#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace reac {
struct Geometry {
    std::size_t channels;
    std::size_t samples;
    bool operator==(const Geometry&) const = default;
};
bool supported(Geometry g);
// Channel-major signed integers; index = channel * samples + time.
std::vector<std::int32_t> decode_pcm(std::span<const std::uint8_t> audio, Geometry g);
// Offline audio-region encoder only: no Ethernet or transmission facilities.
std::vector<std::uint8_t> encode_pcm(std::span<const std::int32_t> pcm, Geometry g);
struct Control {
    std::string kind = "unknown";
    std::string outer_checksum = "not_checked";
    std::string bounds = "not_checked";
    std::string dt1 = "not_applicable";
    std::optional<bool> lengths_match;
    std::optional<bool> terminator_ok;
    std::optional<bool> inner_checksum_ok;
    std::optional<bool> seven_bit_ok;
    std::optional<bool> padding_ok;
    std::optional<std::uint16_t> op;
    std::optional<std::uint16_t> record_length;
    std::optional<std::uint32_t> address;
    std::optional<unsigned> rate_class;
    std::optional<unsigned> rate_hz;
    std::string semantic = "unknown";
    bool valid = true; // integrity/shape only; never permission or session readiness
};
struct Options {
    std::optional<Geometry> geometry;
    // Explicit capture-context allowance, not a guess or FCS validation.
    std::size_t residue_bytes = 0; // only 0 or 2 supported; FCS must be absent
};
struct Inspection {
    std::string status = "invalid";
    bool envelope_valid = false;
    std::array<std::uint8_t, 6> destination{}, source{};
    std::optional<std::uint16_t> vlan_tci, sequence, type;
    std::optional<Geometry> geometry;
    std::size_t raw_length = 0, clean_length = 0;
    bool diagnostic_geometry = false;
    Control control;
    std::vector<std::int32_t> pcm;
    std::vector<std::string> diagnostics;
};
Inspection inspect(std::span<const std::uint8_t> frame, Options options = {});
} // namespace reac
