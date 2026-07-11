#include "audio/resampler.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

namespace {

using local_acr::CanonicalResampler;
using local_acr::CanonicalSampleSink;
using local_acr::Status;

int failures = 0;

void check(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

void check_equal(float actual, float expected, std::string_view message) {
  check(std::bit_cast<std::uint32_t>(actual) == std::bit_cast<std::uint32_t>(expected), message);
}

class CapturingCanonicalSink final : public CanonicalSampleSink {
 public:
  Status consume(std::span<const float> samples) noexcept override {
    output.insert(output.end(), samples.begin(), samples.end());
    return Status::ok_status();
  }

  std::vector<float> output;
};

std::vector<float> make_signal(std::uint32_t rate, std::size_t frames) {
  static_cast<void>(rate);
  std::vector<float> signal(frames);
  for (std::size_t index = 0; index < frames; ++index) {
    const std::uint32_t pattern = (static_cast<std::uint32_t>(index) * 37U + 11U) % 257U;
    signal[index] = static_cast<float>(static_cast<std::int32_t>(pattern) - 128) / 256.0F;
  }
  return signal;
}

std::vector<float> run_partitioned(std::uint32_t rate, std::span<const float> input,
                                   std::span<const std::size_t> chunks,
                                   std::uint32_t* input_latency = nullptr,
                                   std::uint32_t* output_latency = nullptr) {
  std::array<float, 257> output_scratch{};
  CanonicalResampler resampler(rate, output_scratch);
  CapturingCanonicalSink sink;
  check(resampler.valid(), "resampler initializes");

  if (input_latency != nullptr) {
    *input_latency = resampler.input_latency();
  }
  if (output_latency != nullptr) {
    *output_latency = resampler.output_latency();
  }

  std::size_t offset = 0;
  for (const std::size_t chunk : chunks) {
    check(chunk > 0 && chunk <= input.size() - offset, "resampler chunk is in range");
    if (chunk == 0 || chunk > input.size() - offset) {
      return {};
    }
    check(resampler.push(input.subspan(offset, chunk), sink).ok(), "resampler push succeeds");
    offset += chunk;
    const std::uint64_t maximum_live_output =
        static_cast<std::uint64_t>(offset) * CanonicalResampler::kCanonicalRate / rate;
    check(resampler.total_output_samples() <= maximum_live_output,
          "live output never runs ahead of the source timeline");
  }
  check(offset == input.size(), "resampler chunks cover input");
  check(resampler.finish(sink).ok(), "resampler finite finish succeeds");
  return sink.output;
}

std::vector<float> run_one_batch(std::uint32_t rate, std::span<const float> input) {
  const std::array chunks{input.size()};
  return run_partitioned(rate, input, chunks);
}

void q1_23_rounding_is_ties_away_and_saturating() {
  constexpr float step = 1.0F / 8388608.0F;
  constexpr float half_step = 1.0F / 16777216.0F;

  check_equal(local_acr::quantize_q1_23(0.0F), 0.0F, "Q1.23 zero");
  check_equal(local_acr::quantize_q1_23(half_step), step, "positive tie rounds away");
  check_equal(local_acr::quantize_q1_23(-half_step), -step, "negative tie rounds away");
  check_equal(local_acr::quantize_q1_23(2.0F), 8388607.0F / 8388608.0F,
              "positive Q1.23 saturation");
  check_equal(local_acr::quantize_q1_23(-2.0F), -1.0F, "negative Q1.23 saturation");
}

void finite_output_count_is_exact_at_supported_rates() {
  constexpr std::array<std::uint32_t, 4> rates{8000, 44100, 48000, 192000};
  for (const std::uint32_t rate : rates) {
    const std::size_t source_frames = static_cast<std::size_t>(rate / 10U + 37U);
    const std::vector<float> input = make_signal(rate, source_frames);
    const std::vector<float> output = run_one_batch(rate, input);
    const std::size_t expected = static_cast<std::size_t>(
        static_cast<std::uint64_t>(source_frames) * CanonicalResampler::kCanonicalRate / rate);
    check(output.size() == expected, "finite output count follows floor source ratio");

    for (const float sample : output) {
      const float scaled = sample * 8388608.0F;
      check(scaled == std::trunc(scaled), "every canonical sample lies on the Q1.23 grid");
    }
  }
}

void arbitrary_partitions_are_byte_identical() {
  constexpr std::uint32_t rate = 48000;
  const std::vector<float> input = make_signal(rate, 4099);
  const std::vector<float> expected = run_one_batch(rate, input);

  std::uint32_t random = 0x4C414352U;
  for (std::size_t trial = 0; trial < 24; ++trial) {
    std::vector<std::size_t> chunks;
    std::size_t remaining = input.size();
    while (remaining > 0) {
      random = random * 1664525U + 1013904223U;
      const std::size_t proposed = static_cast<std::size_t>(random % 389U) + 1U;
      const std::size_t chunk = std::min(proposed, remaining);
      chunks.push_back(chunk);
      remaining -= chunk;
    }
    const std::vector<float> actual = run_partitioned(rate, input, chunks);
    check(actual == expected, "partitioned resampling is byte-identical");
  }
}

void latency_is_reported_and_stable() {
  constexpr std::uint32_t rate = 44100;
  const std::vector<float> input = make_signal(rate, 1000);
  const std::array chunks{input.size()};
  std::uint32_t input_latency = 0;
  std::uint32_t output_latency = 0;
  static_cast<void>(run_partitioned(rate, input, chunks, &input_latency, &output_latency));

  check(input_latency > 0, "input latency is reported");
  check(output_latency > 0, "output latency is reported");
}

void invalid_configuration_is_rejected() {
  std::array<float, 16> scratch{};
  CanonicalResampler zero_rate(0, scratch);
  CanonicalResampler no_scratch(48000, {});
  check(!zero_rate.valid(), "zero input rate is invalid");
  check(!no_scratch.valid(), "empty output scratch is invalid");
}

void pinned_48000_hz_vector_matches() {
  constexpr std::uint32_t rate = 48000;
  const std::vector<float> input = make_signal(rate, 257);
  const std::array chunks{input.size()};
  std::uint32_t input_latency = 0;
  std::uint32_t output_latency = 0;
  const std::vector<float> output =
      run_partitioned(rate, input, chunks, &input_latency, &output_latency);

  constexpr std::array<std::int32_t, 59> expected_q1_23{
      -1328490, -32321,  -149417, -153402, 16196,   -110901, 113599,  -31477,  188326,
      62885,    252615,  165223,  309836,  275147,  357394,  398799,  384041,  556736,
      347690,   857426,  -238075, -799789, -373225, -602555, -370448, -462033, -334845,
      -344676,  -280602, -243489, -211923, -155102, -132276, -75545,  -46263,  377,
      40528,    78344,   122646,  163250,  196070,  257981,  259181,  362938,  312609,
      476876,   357330,  600652,  387752,  751138,  355993,  -903400, -297163, -630673,
      -293069,  -515232, -228682, -411619, -226439,
  };

  check(input_latency == 176, "48 kHz golden input latency");
  check(output_latency == 40, "48 kHz golden output latency");
  check(output.size() == expected_q1_23.size(), "48 kHz golden output count");
  if (output.size() != expected_q1_23.size()) {
    return;
  }
  for (std::size_t index = 0; index < output.size(); ++index) {
    const auto actual_q1_23 = static_cast<std::int32_t>(output[index] * 8388608.0F);
    check(actual_q1_23 == expected_q1_23[index], "48 kHz golden Q1.23 sample");
  }
}

}  // namespace

int main() {
  q1_23_rounding_is_ties_away_and_saturating();
  finite_output_count_is_exact_at_supported_rates();
  arbitrary_partitions_are_byte_identical();
  latency_is_reported_and_stable();
  invalid_configuration_is_rejected();
  pinned_48000_hz_vector_matches();
  return failures == 0 ? 0 : 1;
}
