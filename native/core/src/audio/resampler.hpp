#ifndef LOCAL_ACR_AUDIO_RESAMPLER_HPP
#define LOCAL_ACR_AUDIO_RESAMPLER_HPP

#include "support/status.hpp"

#include <cstdint>
#include <span>

namespace local_acr {

class CanonicalSampleSink {
 public:
  virtual ~CanonicalSampleSink() = default;
  virtual Status consume(std::span<const float> samples) noexcept = 0;
};

[[nodiscard]] float quantize_q1_23(float sample) noexcept;

class CanonicalResampler final {
 public:
  static constexpr std::uint32_t kCanonicalRate = 11025;

  CanonicalResampler(std::uint32_t input_rate, std::span<float> output_scratch) noexcept;
  ~CanonicalResampler();

  CanonicalResampler(const CanonicalResampler&) = delete;
  CanonicalResampler& operator=(const CanonicalResampler&) = delete;
  CanonicalResampler(CanonicalResampler&&) = delete;
  CanonicalResampler& operator=(CanonicalResampler&&) = delete;

  [[nodiscard]] bool valid() const noexcept;
  [[nodiscard]] std::uint32_t input_latency() const noexcept;
  [[nodiscard]] std::uint32_t output_latency() const noexcept;
  [[nodiscard]] std::uint64_t total_input_samples() const noexcept;
  [[nodiscard]] std::uint64_t total_output_samples() const noexcept;

  [[nodiscard]] Status push(std::span<const float> samples,
                            CanonicalSampleSink& sink) noexcept;
  [[nodiscard]] Status finish(CanonicalSampleSink& sink) noexcept;

 private:
  [[nodiscard]] Status emit(std::uint32_t sample_count,
                            CanonicalSampleSink& sink) noexcept;

  void* state_ = nullptr;
  std::span<float> output_scratch_;
  std::uint32_t input_rate_ = 0;
  std::uint32_t input_latency_ = 0;
  std::uint32_t output_latency_ = 0;
  std::uint64_t total_input_samples_ = 0;
  std::uint64_t total_output_samples_ = 0;
  bool finished_ = false;
};

}  // namespace local_acr

#endif
