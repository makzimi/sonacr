#ifndef LOCAL_ACR_SUPPORT_SHA256_HPP
#define LOCAL_ACR_SUPPORT_SHA256_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace local_acr {

class Sha256 final {
 public:
  void update(std::span<const std::uint8_t> bytes) noexcept;
  [[nodiscard]] std::array<std::uint8_t, 32> finish() noexcept;

 private:
  void compress(const std::uint8_t* block) noexcept;

  std::array<std::uint32_t, 8> state_{
      0x6A09E667U, 0xBB67AE85U, 0x3C6EF372U, 0xA54FF53AU,
      0x510E527FU, 0x9B05688CU, 0x1F83D9ABU, 0x5BE0CD19U};
  std::array<std::uint8_t, 64> buffer_{};
  std::uint64_t bit_count_ = 0;
  std::size_t buffer_size_ = 0;
};

[[nodiscard]] std::string hex_sha256(std::span<const std::uint8_t, 32> digest);

}  // namespace local_acr

#endif
