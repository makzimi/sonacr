#include "fingerprint/spectrum.hpp"

#include "fingerprint/frequency_weights_q16.hpp"
#include "fingerprint/log_q16.hpp"

#include "kiss_fftr.h"

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace local_acr {

namespace {

std::int32_t saturate_i64_to_i32(const std::int64_t value) noexcept {
  if (value > static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::max())) {
    return std::numeric_limits<std::int32_t>::max();
  }
  if (value < static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::min())) {
    return std::numeric_limits<std::int32_t>::min();
  }
  return static_cast<std::int32_t>(value);
}

}  // namespace

float clamp_power_floor(const float power) noexcept {
  if (!(power >= kMinPower)) {
    return kMinPower;
  }
  return power;
}

std::int32_t saturating_add_q16(const std::int32_t lhs, const std::int32_t rhs) noexcept {
  return saturate_i64_to_i32(static_cast<std::int64_t>(lhs) + static_cast<std::int64_t>(rhs));
}

std::int32_t saturating_sub_q16(const std::int32_t lhs, const std::int32_t rhs) noexcept {
  return saturate_i64_to_i32(static_cast<std::int64_t>(lhs) - static_cast<std::int64_t>(rhs));
}

std::int32_t mul_q16(const std::int32_t lhs, const std::int32_t rhs) noexcept {
  const std::int64_t product = static_cast<std::int64_t>(lhs) * static_cast<std::int64_t>(rhs);
  const std::int64_t rounded =
      product >= 0 ? product + (1LL << 15) : product - (1LL << 15);
  return saturate_i64_to_i32(rounded / (1LL << 16));
}

std::int32_t ln_binary32_q16(const float value) noexcept {
  const std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
  const std::uint32_t raw_exponent = (bits >> 23U) & 0xFFU;
  const std::uint32_t mantissa = bits & 0x7FFFFFU;

  if (raw_exponent == 0U || raw_exponent == 0xFFU) {
    return ln_binary32_q16(kMinPower);
  }

  const std::int32_t exponent = static_cast<std::int32_t>(raw_exponent) - 127;
  const std::size_t mantissa_index = static_cast<std::size_t>(mantissa >> 7U);
  const std::int64_t weighted_exponent =
      static_cast<std::int64_t>(exponent) * static_cast<std::int64_t>(kLn2Q16);
  return saturating_add_q16(saturate_i64_to_i32(weighted_exponent),
                            kLnMantissaQ16[mantissa_index]);
}

std::int32_t ln_power_q16(const float power) noexcept {
  return ln_binary32_q16(clamp_power_floor(power));
}

SpectrumAnalyzer::SpectrumAnalyzer() noexcept {
  fft_cfg_ = kiss_fftr_alloc(static_cast<int>(kWindowSize), 0, nullptr, nullptr);
}

SpectrumAnalyzer::~SpectrumAnalyzer() {
  if (fft_cfg_ != nullptr) {
    kiss_fftr_free(fft_cfg_);
  }
}

Status SpectrumAnalyzer::analyze_log(const AnalysisFrame& frame, LogSpectrum& output) noexcept {
  if (fft_cfg_ == nullptr) {
    return Status::native_engine_failure();
  }

  for (std::size_t index = 0; index < kWindowSize; ++index) {
    if (!std::isfinite(frame.samples[index])) {
      return Status::invalid_argument();
    }
    time_buffer_[index] = frame.samples[index];
  }

  kiss_fftr(fft_cfg_, time_buffer_.data(), reinterpret_cast<kiss_fft_cpx*>(freq_buffer_.data()));

  output.time_frame = frame.time_frame;
  for (std::size_t bin = 1; bin <= kBinCount; ++bin) {
    const double real = static_cast<double>(freq_buffer_[bin].r);
    const double imag = static_cast<double>(freq_buffer_[bin].i);
    const float power = static_cast<float>((real * real) + (imag * imag));
    const std::int32_t weighted =
        saturating_add_q16(ln_power_q16(power), kLnWeightQ16[bin]);
    output.q16[bin - 1] = weighted;
  }

  return Status::ok_status();
}

LogSpectrum SpectrumAnalyzer::analyze_log(const AnalysisFrame& frame) noexcept {
  LogSpectrum output{};
  static_cast<void>(analyze_log(frame, output));
  return output;
}

Status SpectrumAnalyzer::process(const AnalysisFrame& frame, DecisionSpectrum& output) noexcept {
  LogSpectrum log_spectrum{};
  const Status status = analyze_log(frame, log_spectrum);
  if (!status.ok()) {
    return status;
  }
  output = temporal_filter_.process(log_spectrum);
  return Status::ok_status();
}

void SpectrumAnalyzer::reset() noexcept {
  temporal_filter_.reset();
}

}  // namespace local_acr
