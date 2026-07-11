#include "audio/resampler.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

#define OUTSIDE_SPEEX
#define RANDOM_PREFIX lacr_speex
extern "C" {
#include "speex_resampler.h"
}

namespace local_acr {
namespace {

constexpr std::uint32_t kMinimumInputRate = 8000;
constexpr std::uint32_t kMaximumInputRate = 192000;
constexpr int kResamplerQuality = 5;
constexpr double kQ1_23Scale = 8388608.0;
constexpr std::int64_t kQ1_23Minimum = -8388608;
constexpr std::int64_t kQ1_23Maximum = 8388607;

[[nodiscard]] SpeexResamplerState* as_speex(void* state) noexcept {
  return static_cast<SpeexResamplerState*>(state);
}

[[nodiscard]] std::uint64_t canonical_count(std::uint64_t source_count,
                                            std::uint32_t source_rate) noexcept {
  if (source_rate == 0 ||
      source_count > std::numeric_limits<std::uint64_t>::max() / CanonicalResampler::kCanonicalRate) {
    return std::numeric_limits<std::uint64_t>::max();
  }
  return source_count * CanonicalResampler::kCanonicalRate / source_rate;
}

}  // namespace

float quantize_q1_23(float sample) noexcept {
  const double scaled = static_cast<double>(sample) * kQ1_23Scale;
  const double rounded = scaled >= 0.0 ? std::floor(scaled + 0.5) : std::ceil(scaled - 0.5);
  const auto quantized = static_cast<std::int64_t>(
      std::clamp(rounded, static_cast<double>(kQ1_23Minimum), static_cast<double>(kQ1_23Maximum)));
  return static_cast<float>(static_cast<double>(quantized) / kQ1_23Scale);
}

CanonicalResampler::CanonicalResampler(std::uint32_t input_rate,
                                       std::span<float> output_scratch) noexcept
    : output_scratch_(output_scratch), input_rate_(input_rate) {
  if (input_rate < kMinimumInputRate || input_rate > kMaximumInputRate || output_scratch.empty() ||
      output_scratch.size() > std::numeric_limits<spx_uint32_t>::max()) {
    return;
  }

  int error = RESAMPLER_ERR_SUCCESS;
  SpeexResamplerState* state =
      speex_resampler_init(1, input_rate, kCanonicalRate, kResamplerQuality, &error);
  if (state == nullptr || error != RESAMPLER_ERR_SUCCESS) {
    if (state != nullptr) {
      speex_resampler_destroy(state);
    }
    return;
  }
  if (speex_resampler_skip_zeros(state) != RESAMPLER_ERR_SUCCESS) {
    speex_resampler_destroy(state);
    return;
  }

  const int input_latency = speex_resampler_get_input_latency(state);
  const int output_latency = speex_resampler_get_output_latency(state);
  if (input_latency < 0 || output_latency < 0) {
    speex_resampler_destroy(state);
    return;
  }

  state_ = state;
  input_latency_ = static_cast<std::uint32_t>(input_latency);
  output_latency_ = static_cast<std::uint32_t>(output_latency);
}

CanonicalResampler::~CanonicalResampler() {
  if (state_ != nullptr) {
    speex_resampler_destroy(as_speex(state_));
  }
}

bool CanonicalResampler::valid() const noexcept {
  return state_ != nullptr;
}

std::uint32_t CanonicalResampler::input_latency() const noexcept {
  return input_latency_;
}

std::uint32_t CanonicalResampler::output_latency() const noexcept {
  return output_latency_;
}

std::uint64_t CanonicalResampler::total_input_samples() const noexcept {
  return total_input_samples_;
}

std::uint64_t CanonicalResampler::total_output_samples() const noexcept {
  return total_output_samples_;
}

Status CanonicalResampler::emit(std::uint32_t sample_count,
                                CanonicalSampleSink& sink) noexcept {
  for (std::uint32_t index = 0; index < sample_count; ++index) {
    float& sample = output_scratch_[index];
    if (!std::isfinite(sample)) {
      return Status::native_engine_failure();
    }
    sample = quantize_q1_23(sample);
  }

  const Status status = sink.consume(output_scratch_.first(sample_count));
  if (!status.ok()) {
    return status;
  }
  total_output_samples_ += sample_count;
  return Status::ok_status();
}

Status CanonicalResampler::push(std::span<const float> samples,
                                CanonicalSampleSink& sink) noexcept {
  if (!valid()) {
    return Status::native_engine_failure();
  }
  if (finished_) {
    return Status::invalid_state();
  }
  if (samples.empty() || samples.size() > std::numeric_limits<spx_uint32_t>::max() ||
      total_input_samples_ > std::numeric_limits<std::uint64_t>::max() - samples.size()) {
    return Status::invalid_argument();
  }

  std::size_t offset = 0;
  while (offset < samples.size()) {
    spx_uint32_t input_count = static_cast<spx_uint32_t>(samples.size() - offset);
    spx_uint32_t output_count = static_cast<spx_uint32_t>(output_scratch_.size());
    const int error = speex_resampler_process_float(as_speex(state_), 0, samples.data() + offset,
                                                    &input_count, output_scratch_.data(), &output_count);
    if (error != RESAMPLER_ERR_SUCCESS || (input_count == 0 && output_count == 0)) {
      return Status::native_engine_failure();
    }

    offset += input_count;
    total_input_samples_ += input_count;
    const Status emit_status = emit(output_count, sink);
    if (!emit_status.ok()) {
      return emit_status;
    }
  }
  return Status::ok_status();
}

Status CanonicalResampler::finish(CanonicalSampleSink& sink) noexcept {
  if (!valid()) {
    return Status::native_engine_failure();
  }
  if (finished_) {
    return Status::invalid_state();
  }
  finished_ = true;

  const std::uint64_t target_output = canonical_count(total_input_samples_, input_rate_);
  if (target_output == std::numeric_limits<std::uint64_t>::max() ||
      total_output_samples_ > target_output) {
    return Status::native_engine_failure();
  }

  constexpr std::array<float, 256> zeros{};
  std::uint32_t no_output_iterations = 0;
  while (total_output_samples_ < target_output) {
    spx_uint32_t input_count = static_cast<spx_uint32_t>(zeros.size());
    const std::uint64_t remaining = target_output - total_output_samples_;
    spx_uint32_t output_count = static_cast<spx_uint32_t>(
        std::min<std::uint64_t>(remaining, output_scratch_.size()));
    const int error = speex_resampler_process_float(as_speex(state_), 0, zeros.data(), &input_count,
                                                    output_scratch_.data(), &output_count);
    if (error != RESAMPLER_ERR_SUCCESS) {
      return Status::native_engine_failure();
    }
    if (output_count == 0) {
      ++no_output_iterations;
      if (no_output_iterations > 4096) {
        return Status::native_engine_failure();
      }
      continue;
    }
    no_output_iterations = 0;
    const Status emit_status = emit(output_count, sink);
    if (!emit_status.ok()) {
      return emit_status;
    }
  }
  return Status::ok_status();
}

}  // namespace local_acr
