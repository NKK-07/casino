// Sha256.hpp - small self-contained SHA-256, used for the provably-fair shuffle
// commitments and for file integrity checksums.
#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace casino {

std::array<std::uint8_t, 32> sha256(const std::string& data);
std::string sha256Hex(const std::string& data);
// Interprets the first 8 bytes of a digest as a big-endian 64-bit number.
std::uint64_t digestToSeed(const std::array<std::uint8_t, 32>& digest);

}  // namespace casino
