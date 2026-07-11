#ifndef LOCAL_ACR_AUDIO_FRAME_STREAM_HPP
#define LOCAL_ACR_AUDIO_FRAME_STREAM_HPP

#include "support/status.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace local_acr {

struct AnalysisFrame final {
  std::uint64_t time_frame;
  std::uint64_t first_canonical_sample;
  std::array<float, 512> samples;
};

class AnalysisFrameSink {
 public:
  virtual ~AnalysisFrameSink() = default;
  virtual Status consume(const AnalysisFrame& frame) noexcept = 0;
};

class FrameStream final {
 public:
  static constexpr std::size_t kWindowSize = 512;
  static constexpr std::size_t kHopSize = 128;

  [[nodiscard]] Status push(std::span<const float> samples,
                            AnalysisFrameSink& sink) noexcept;
  [[nodiscard]] Status finish() noexcept;
  void reset() noexcept;

  [[nodiscard]] std::uint64_t total_canonical_samples() const noexcept;
  [[nodiscard]] std::uint64_t emitted_frames() const noexcept;

 private:
  [[nodiscard]] Status emit(AnalysisFrameSink& sink) noexcept;

  std::array<float, kWindowSize> ring_{};
  std::uint64_t total_canonical_samples_ = 0;
  std::uint64_t next_frame_start_ = 0;
  std::uint64_t emitted_frames_ = 0;
  bool finished_ = false;
  bool failed_ = false;
};

}  // namespace local_acr

#endif
