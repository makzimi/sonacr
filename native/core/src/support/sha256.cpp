#include "support/sha256.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace local_acr {

namespace {

constexpr std::array<std::uint32_t, 64> kRoundConstants{
    0x428A2F98U, 0x71374491U, 0xB5C0FBCFU, 0xE9B5DBA5U, 0x3956C25BU, 0x59F111F1U,
    0x923F82A4U, 0xAB1C5ED5U, 0xD807AA98U, 0x12835B01U, 0x243185BEU, 0x550C7DC3U,
    0x72BE5D74U, 0x80DEB1FEU, 0x9BDC06A7U, 0xC19BF174U, 0xE49B69C1U, 0xEFBE4786U,
    0x0FC19DC6U, 0x240CA1CCU, 0x2DE92C6FU, 0x4A7484AAU, 0x5CB0A9DCU, 0x76F988DAU,
    0x983E5152U, 0xA831C66DU, 0xB00327C8U, 0xBF597FC7U, 0xC6E00BF3U, 0xD5A79147U,
    0x06CA6351U, 0x14292967U, 0x27B70A85U, 0x2E1B2138U, 0x4D2C6DFCU, 0x53380D13U,
    0x650A7354U, 0x766A0ABBU, 0x81C2C92EU, 0x92722C85U, 0xA2BFE8A1U, 0xA81A664BU,
    0xC24B8B70U, 0xC76C51A3U, 0xD192E819U, 0xD6990624U, 0xF40E3585U, 0x106AA070U,
    0x19A4C116U, 0x1E376C08U, 0x2748774CU, 0x34B0BCB5U, 0x391C0CB3U, 0x4ED8AA4AU,
    0x5B9CCA4FU, 0x682E6FF3U, 0x748F82EEU, 0x78A5636FU, 0x84C87814U, 0x8CC70208U,
    0x90BEFFFAU, 0xA4506CEBU, 0xBEF9A3F7U, 0xC67178F2U};

[[nodiscard]] std::uint32_t rotate_right(std::uint32_t value, std::uint32_t bits) noexcept {
  return (value >> bits) | (value << (32U - bits));
}

[[nodiscard]] std::uint32_t read_be32(const std::uint8_t* data) noexcept {
  return (static_cast<std::uint32_t>(data[0]) << 24U) |
         (static_cast<std::uint32_t>(data[1]) << 16U) |
         (static_cast<std::uint32_t>(data[2]) << 8U) |
         static_cast<std::uint32_t>(data[3]);
}

void write_be32(std::uint32_t value, std::uint8_t* out) noexcept {
  out[0] = static_cast<std::uint8_t>(value >> 24U);
  out[1] = static_cast<std::uint8_t>(value >> 16U);
  out[2] = static_cast<std::uint8_t>(value >> 8U);
  out[3] = static_cast<std::uint8_t>(value);
}

void write_be64(std::uint64_t value, std::uint8_t* out) noexcept {
  for (std::size_t i = 0; i < 8; ++i) {
    out[i] = static_cast<std::uint8_t>(value >> ((7U - i) * 8U));
  }
}

}  // namespace

void Sha256::update(std::span<const std::uint8_t> bytes) noexcept {
  bit_count_ += static_cast<std::uint64_t>(bytes.size()) * 8U;
  std::size_t offset = 0;
  while (offset < bytes.size()) {
    const std::size_t copy_count = std::min<std::size_t>(64U - buffer_size_, bytes.size() - offset);
    std::memcpy(buffer_.data() + buffer_size_, bytes.data() + offset, copy_count);
    buffer_size_ += copy_count;
    offset += copy_count;
    if (buffer_size_ == 64U) {
      compress(buffer_.data());
      buffer_size_ = 0;
    }
  }
}

std::array<std::uint8_t, 32> Sha256::finish() noexcept {
  const std::uint64_t final_bit_count = bit_count_;
  buffer_[buffer_size_++] = 0x80U;
  if (buffer_size_ > 56U) {
    std::fill(buffer_.begin() + static_cast<std::ptrdiff_t>(buffer_size_), buffer_.end(), 0);
    compress(buffer_.data());
    buffer_size_ = 0;
  }
  std::fill(buffer_.begin() + static_cast<std::ptrdiff_t>(buffer_size_), buffer_.begin() + 56, 0);
  write_be64(final_bit_count, buffer_.data() + 56);
  compress(buffer_.data());

  std::array<std::uint8_t, 32> digest{};
  for (std::size_t i = 0; i < state_.size(); ++i) {
    write_be32(state_[i], digest.data() + i * 4U);
  }
  return digest;
}

void Sha256::compress(const std::uint8_t* block) noexcept {
  std::array<std::uint32_t, 64> w{};
  for (std::size_t i = 0; i < 16; ++i) {
    w[i] = read_be32(block + i * 4U);
  }
  for (std::size_t i = 16; i < 64; ++i) {
    const std::uint32_t s0 = rotate_right(w[i - 15U], 7) ^ rotate_right(w[i - 15U], 18) ^
                             (w[i - 15U] >> 3U);
    const std::uint32_t s1 = rotate_right(w[i - 2U], 17) ^ rotate_right(w[i - 2U], 19) ^
                             (w[i - 2U] >> 10U);
    w[i] = w[i - 16U] + s0 + w[i - 7U] + s1;
  }

  std::uint32_t a = state_[0];
  std::uint32_t b = state_[1];
  std::uint32_t c = state_[2];
  std::uint32_t d = state_[3];
  std::uint32_t e = state_[4];
  std::uint32_t f = state_[5];
  std::uint32_t g = state_[6];
  std::uint32_t h = state_[7];

  for (std::size_t i = 0; i < 64; ++i) {
    const std::uint32_t s1 = rotate_right(e, 6) ^ rotate_right(e, 11) ^ rotate_right(e, 25);
    const std::uint32_t ch = (e & f) ^ ((~e) & g);
    const std::uint32_t temp1 = h + s1 + ch + kRoundConstants[i] + w[i];
    const std::uint32_t s0 = rotate_right(a, 2) ^ rotate_right(a, 13) ^ rotate_right(a, 22);
    const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
    const std::uint32_t temp2 = s0 + maj;
    h = g;
    g = f;
    f = e;
    e = d + temp1;
    d = c;
    c = b;
    b = a;
    a = temp1 + temp2;
  }

  state_[0] += a;
  state_[1] += b;
  state_[2] += c;
  state_[3] += d;
  state_[4] += e;
  state_[5] += f;
  state_[6] += g;
  state_[7] += h;
}

std::string hex_sha256(std::span<const std::uint8_t, 32> digest) {
  std::array<char, 65> out{};
  for (std::size_t i = 0; i < digest.size(); ++i) {
    static_cast<void>(std::snprintf(out.data() + i * 2U, 3, "%02x", digest[i]));
  }
  return std::string(out.data(), 64);
}

}  // namespace local_acr
