#include "fingerprint/frequency_weights_q16.hpp"
#include "fingerprint/log_q16.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string_view>

namespace {

int failures = 0;

void check(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

std::int32_t reference_round_q16(double value) {
  const double scaled = value * 65536.0;
  const double rounded = scaled >= 0.0 ? std::floor(scaled + 0.5) : std::ceil(scaled - 0.5);
  if (rounded > static_cast<double>(std::numeric_limits<std::int32_t>::max())) {
    return std::numeric_limits<std::int32_t>::max();
  }
  if (rounded < static_cast<double>(std::numeric_limits<std::int32_t>::min())) {
    return std::numeric_limits<std::int32_t>::min();
  }
  return static_cast<std::int32_t>(rounded);
}

std::int32_t reference_log_q16(float value) {
  return reference_round_q16(std::log(static_cast<double>(value)));
}

std::int32_t reference_table_log_q16(float value) {
  const std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
  const std::int32_t exponent = static_cast<std::int32_t>((bits >> 23U) & 0xFFU) - 127;
  const std::size_t mantissa_index = static_cast<std::size_t>((bits & 0x7FFFFFU) >> 7U);
  return local_acr::saturating_add_q16(exponent * local_acr::kLn2Q16,
                                       local_acr::kLnMantissaQ16[mantissa_index]);
}

void constants_are_pinned() {
  check(local_acr::kLn2Q16 == 45426, "ln2 Q16 is pinned");
  check(local_acr::kMinPowerBits == 0x2B8CBCCC, "1e-12 binary32 floor bits are pinned");
  check(std::bit_cast<std::uint32_t>(local_acr::kMinPower) == local_acr::kMinPowerBits,
        "minimum power has checked binary32 representation");
}

void mantissa_table_boundaries_are_checked() {
  check(local_acr::kLnMantissaQ16.size() == 65536, "mantissa table size");
  check(local_acr::kLnMantissaQ16.front() == 0, "ln mantissa table starts at ln(1)");
  check(local_acr::kLnMantissaQ16[1] == reference_round_q16(std::log1p(1.0 / 65536.0)),
        "ln mantissa table first step");
  check(local_acr::kLnMantissaQ16[32768] == reference_round_q16(std::log(1.5)),
        "ln mantissa table midpoint");
  check(local_acr::kLnMantissaQ16.back() == reference_round_q16(std::log1p(65535.0 / 65536.0)),
        "ln mantissa table final entry");
}

void binary32_log_uses_exponent_and_mantissa_bits() {
  check(local_acr::ln_binary32_q16(1.0F) == 0, "ln(1) is zero");
  check(local_acr::ln_binary32_q16(2.0F) == local_acr::kLn2Q16, "ln(2) uses exponent");
  check(local_acr::ln_binary32_q16(0.5F) == -local_acr::kLn2Q16, "ln(0.5) uses exponent");
  check(local_acr::ln_binary32_q16(1.5F) == reference_log_q16(1.5F), "ln(1.5) uses mantissa");
  check(local_acr::ln_binary32_q16(0.75F) == reference_table_log_q16(0.75F),
        "ln(0.75) combines exponent and quantized mantissa");
}

void power_floor_is_applied_before_log() {
  check(local_acr::clamp_power_floor(0.0F) == local_acr::kMinPower, "zero power clamps to floor");
  check(local_acr::clamp_power_floor(-1.0F) == local_acr::kMinPower, "negative power clamps to floor");
  check(local_acr::clamp_power_floor(1.0e-20F) == local_acr::kMinPower,
        "subfloor positive power clamps to floor");
  check(local_acr::clamp_power_floor(1.0F) == 1.0F, "ordinary power is unchanged");
  check(local_acr::ln_power_q16(0.0F) == local_acr::ln_binary32_q16(local_acr::kMinPower),
        "zero log uses the floor value");
}

void frequency_weights_are_checked() {
  check(local_acr::kLnWeightQ16.size() == 256, "frequency weight count includes DC slot");
  for (std::size_t bin : std::array<std::size_t, 5>{0, 1, 16, 128, 255}) {
    const std::int32_t expected = reference_round_q16(std::log(static_cast<double>(bin + 16)));
    check(local_acr::kLnWeightQ16[bin] == expected, "frequency weight matches ln(bin+16)");
  }
}

void saturating_q16_arithmetic_is_defined() {
  check(local_acr::saturating_add_q16(100, 23) == 123, "small addition");
  check(local_acr::saturating_add_q16(std::numeric_limits<std::int32_t>::max(), 1) ==
            std::numeric_limits<std::int32_t>::max(),
        "positive addition saturates");
  check(local_acr::saturating_add_q16(std::numeric_limits<std::int32_t>::min(), -1) ==
            std::numeric_limits<std::int32_t>::min(),
        "negative addition saturates");
  check(local_acr::mul_q16(65536, 65536) == 65536, "one times one");
  check(local_acr::mul_q16(32768, 32768) == 16384, "half times half");
  check(local_acr::mul_q16(1, 32768) == 1, "positive half tie rounds away from zero");
  check(local_acr::mul_q16(-1, 32768) == -1, "negative half tie rounds away from zero");
}

}  // namespace

int main() {
  constants_are_pinned();
  mantissa_table_boundaries_are_checked();
  binary32_log_uses_exponent_and_mantissa_bits();
  power_floor_is_applied_before_log();
  frequency_weights_are_checked();
  saturating_q16_arithmetic_is_defined();
  return failures == 0 ? 0 : 1;
}
