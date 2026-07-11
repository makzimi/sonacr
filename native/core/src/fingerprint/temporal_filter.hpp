#ifndef LOCAL_ACR_FINGERPRINT_TEMPORAL_FILTER_HPP
#define LOCAL_ACR_FINGERPRINT_TEMPORAL_FILTER_HPP

#include "fingerprint/log_q16.hpp"

#include <array>
#include <cstdint>

namespace local_acr {

struct LogSpectrum final {
  std::uint64_t time_frame = 0;
  std::array<std::int32_t, 255> q16{};
};

struct DecisionSpectrum final {
  std::uint64_t time_frame = 0;
  std::array<std::int32_t, 255> q16{};
};

class TemporalFilter final {
 public:
  static constexpr std::size_t kBinCount = 255;
  static constexpr std::int32_t kHpfPoleQ16 = 64225;

  [[nodiscard]] DecisionSpectrum process(const LogSpectrum& input) noexcept {
    DecisionSpectrum output{};
    output.time_frame = input.time_frame;

    if (!initialized_) {
      previous_input_ = input.q16;
      previous_output_.fill(0);
      initialized_ = true;
      return output;
    }

    for (std::size_t index = 0; index < kBinCount; ++index) {
      const std::int32_t delta = saturating_sub_q16(input.q16[index], previous_input_[index]);
      const std::int32_t decayed = mul_q16(kHpfPoleQ16, previous_output_[index]);
      output.q16[index] = saturating_add_q16(delta, decayed);
    }

    previous_input_ = input.q16;
    previous_output_ = output.q16;
    return output;
  }

  void reset() noexcept {
    previous_input_.fill(0);
    previous_output_.fill(0);
    initialized_ = false;
  }

 private:
  std::array<std::int32_t, kBinCount> previous_input_{};
  std::array<std::int32_t, kBinCount> previous_output_{};
  bool initialized_ = false;
};

}  // namespace local_acr

#endif
