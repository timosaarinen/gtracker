#include "gtracker/hash.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace gtracker {
namespace {
// SHA-256 functions/constants: FIPS 180-4 sections 4.1.2, 4.2.2, 5.3.3, 6.2.
// https://nvlpubs.nist.gov/nistpubs/fips/nist.fips.180-4.pdf
constexpr std::array<std::uint32_t, 64> constants = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U
};

std::uint32_t rotate(std::uint32_t value, unsigned count) {
    return (value >> count) | (value << (32U - count));
}

void block(std::array<std::uint32_t, 8>& state, const unsigned char* bytes) {
    std::array<std::uint32_t, 64> words{};
    for (std::size_t i = 0; i < 16; ++i) {
        words[i] = (std::uint32_t{bytes[i * 4]} << 24) | (std::uint32_t{bytes[i * 4 + 1]} << 16)
                 | (std::uint32_t{bytes[i * 4 + 2]} << 8) | std::uint32_t{bytes[i * 4 + 3]};
    }
    for (std::size_t i = 16; i < words.size(); ++i) {
        const auto x = words[i - 15];
        const auto y = words[i - 2];
        const auto first = rotate(x, 7) ^ rotate(x, 18) ^ (x >> 3);
        const auto second = rotate(y, 17) ^ rotate(y, 19) ^ (y >> 10);
        words[i] = words[i - 16] + first + words[i - 7] + second;
    }
    auto a = state[0], b = state[1], c = state[2], d = state[3];
    auto e = state[4], f = state[5], g = state[6], h = state[7];
    for (std::size_t i = 0; i < words.size(); ++i) {
        const auto first = h + (rotate(e, 6) ^ rotate(e, 11) ^ rotate(e, 25))
                         + ((e & f) ^ (~e & g)) + constants[i] + words[i];
        const auto second = (rotate(a, 2) ^ rotate(a, 13) ^ rotate(a, 22))
                          + ((a & b) ^ (a & c) ^ (b & c));
        h = g; g = f; f = e; e = d + first;
        d = c; c = b; b = a; a = first + second;
    }
    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}
}

std::string sha256(std::string_view bytes) {
    if (bytes.size() > std::numeric_limits<std::uint64_t>::max() / 8) {
        throw std::length_error("SHA-256 input exceeds its length representation");
    }
    std::array<std::uint32_t, 8> state = {
        0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
        0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U
    };
    const auto* data = reinterpret_cast<const unsigned char*>(bytes.data());
    std::size_t offset = 0;
    while (bytes.size() - offset >= 64) {
        block(state, data + offset);
        offset += 64;
    }
    std::array<unsigned char, 128> tail{};
    const auto remaining = bytes.size() - offset;
    if (remaining) std::memcpy(tail.data(), data + offset, remaining);
    tail[remaining] = 0x80;
    const std::size_t padded = remaining < 56 ? 64 : 128;
    const auto bit_count = static_cast<std::uint64_t>(bytes.size()) * 8;
    for (unsigned i = 0; i < 8; ++i) tail[padded - 1 - i] = static_cast<unsigned char>(bit_count >> (i * 8));
    block(state, tail.data());
    if (padded == 128) block(state, tail.data() + 64);
    constexpr char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(64);
    for (const auto word : state) {
        for (int shift = 28; shift >= 0; shift -= 4) result.push_back(hex[(word >> shift) & 15]);
    }
    return result;
}
} // namespace gtracker
