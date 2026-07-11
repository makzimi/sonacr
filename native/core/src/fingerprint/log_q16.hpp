#ifndef LOCAL_ACR_FINGERPRINT_LOG_Q16_HPP
#define LOCAL_ACR_FINGERPRINT_LOG_Q16_HPP

#include <array>
#include <cstdint>

namespace local_acr {

constexpr std::int32_t kLn2Q16 = 45426;
constexpr std::uint32_t kMinPowerBits = 0x2B8CBCCC;
constexpr float kMinPower = 9.999999960041972e-13F;

extern const std::array<std::int32_t, 65536> kLnMantissaQ16;

[[nodiscard]] float clamp_power_floor(float power) noexcept;
[[nodiscard]] std::int32_t saturating_add_q16(std::int32_t lhs, std::int32_t rhs) noexcept;
[[nodiscard]] std::int32_t saturating_sub_q16(std::int32_t lhs, std::int32_t rhs) noexcept;
[[nodiscard]] std::int32_t mul_q16(std::int32_t lhs, std::int32_t rhs) noexcept;
[[nodiscard]] std::int32_t ln_binary32_q16(float value) noexcept;
[[nodiscard]] std::int32_t ln_power_q16(float power) noexcept;

}  // namespace local_acr

#endif
