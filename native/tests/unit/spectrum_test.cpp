#include "audio/frame_stream.hpp"
#include "fingerprint/frequency_weights_q16.hpp"
#include "fingerprint/log_q16.hpp"
#include "fingerprint/spectrum.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <span>
#include <string_view>

namespace {

int failures = 0;

void check(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

local_acr::AnalysisFrame make_frame(std::uint64_t time_frame) {
  local_acr::AnalysisFrame frame{};
  frame.time_frame = time_frame;
  frame.first_canonical_sample = time_frame * 128;
  return frame;
}

void zero_frame_uses_power_floor_and_excludes_dc_and_nyquist() {
  local_acr::SpectrumAnalyzer analyzer;
  const local_acr::AnalysisFrame frame = make_frame(3);

  const local_acr::LogSpectrum spectrum = analyzer.analyze_log(frame);

  check(spectrum.time_frame == 3, "log spectrum preserves time frame");
  check(spectrum.q16.size() == 255, "log spectrum exposes bins 1 through 255");
  for (std::size_t bin = 1; bin <= 255; ++bin) {
    const std::int32_t expected =
        local_acr::saturating_add_q16(local_acr::ln_power_q16(0.0F), local_acr::kLnWeightQ16[bin]);
    check(spectrum.q16[bin - 1] == expected, "zero frame uses floored weighted log power");
  }
}

void impulse_has_flat_fft_power_before_frequency_weighting() {
  local_acr::SpectrumAnalyzer analyzer;
  local_acr::AnalysisFrame frame = make_frame(0);
  frame.samples[0] = 1.0F;

  const local_acr::LogSpectrum spectrum = analyzer.analyze_log(frame);

  const std::int32_t bin1_unweighted =
      local_acr::saturating_add_q16(spectrum.q16[0], -local_acr::kLnWeightQ16[1]);
  const std::int32_t bin255_unweighted =
      local_acr::saturating_add_q16(spectrum.q16[254], -local_acr::kLnWeightQ16[255]);
  check(bin1_unweighted == local_acr::ln_power_q16(1.0F), "impulse bin 1 power is one");
  check(bin255_unweighted == local_acr::ln_power_q16(1.0F), "impulse bin 255 power is one");
}

void bin_centered_sine_concentrates_power_in_that_bin() {
  local_acr::SpectrumAnalyzer analyzer;
  local_acr::AnalysisFrame frame = make_frame(0);
  constexpr std::size_t target_bin = 17;
  constexpr double two_pi = 6.283185307179586476925286766559;
  for (std::size_t n = 0; n < frame.samples.size(); ++n) {
    frame.samples[n] = static_cast<float>(std::sin(two_pi * static_cast<double>(target_bin * n) /
                                                   static_cast<double>(frame.samples.size())));
  }

  const local_acr::LogSpectrum spectrum = analyzer.analyze_log(frame);
  const auto strongest = std::max_element(spectrum.q16.begin(), spectrum.q16.end());
  const std::size_t strongest_bin = static_cast<std::size_t>(strongest - spectrum.q16.begin()) + 1;

  check(strongest_bin == target_bin, "bin-centered sine selects its FFT bin");
  check(spectrum.q16[target_bin - 1] > spectrum.q16[target_bin - 2], "target beats lower neighbor");
  check(spectrum.q16[target_bin - 1] > spectrum.q16[target_bin], "target beats upper neighbor");
}

void nonfinite_input_is_rejected_without_state_advance() {
  local_acr::SpectrumAnalyzer analyzer;
  local_acr::AnalysisFrame bad = make_frame(0);
  bad.samples[10] = std::numeric_limits<float>::quiet_NaN();
  local_acr::LogSpectrum ignored{};

  check(analyzer.analyze_log(bad, ignored).code() == local_acr::StatusCode::InvalidArgument,
        "nonfinite frame is rejected");

  local_acr::AnalysisFrame good = make_frame(4);
  good.samples[0] = 1.0F;
  local_acr::DecisionSpectrum decision{};
  check(analyzer.process(good, decision).ok(), "valid frame after rejection succeeds");
  check(decision.time_frame == 4, "valid frame after rejection preserves timeline");
  for (const std::int32_t value : decision.q16) {
    check(value == 0, "first accepted decision frame is zero after rejection");
  }
}

void process_applies_initial_zero_filter_then_recurrence() {
  local_acr::SpectrumAnalyzer analyzer;
  local_acr::AnalysisFrame first = make_frame(0);
  first.samples[0] = 1.0F;
  local_acr::DecisionSpectrum y0{};
  check(analyzer.process(first, y0).ok(), "first spectral process succeeds");
  for (const std::int32_t value : y0.q16) {
    check(value == 0, "first decision frame is zero");
  }

  local_acr::AnalysisFrame second = make_frame(1);
  second.samples[0] = 0.5F;
  local_acr::DecisionSpectrum y1{};
  check(analyzer.process(second, y1).ok(), "second spectral process succeeds");
  check(y1.q16[0] < 0, "lower second-frame power produces negative response");
}

}  // namespace

int main() {
  zero_frame_uses_power_floor_and_excludes_dc_and_nyquist();
  impulse_has_flat_fft_power_before_frequency_weighting();
  bin_centered_sine_concentrates_power_in_that_bin();
  nonfinite_input_is_rejected_without_state_advance();
  process_applies_initial_zero_filter_then_recurrence();
  return failures == 0 ? 0 : 1;
}
