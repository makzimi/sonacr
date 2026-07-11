#include "audio/frame_stream.hpp"

#include "fingerprint/hann_q31.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace local_acr {
namespace {

constexpr double kQ31Scale = 2147483648.0;

}  // namespace

Status FrameStream::push(std::span<const float> samples,
                         AnalysisFrameSink& sink) noexcept {
  if (finished_ || failed_) {
    return Status::invalid_state();
  }
  if (samples.empty() ||
      total_canonical_samples_ > std::numeric_limits<std::uint64_t>::max() - samples.size()) {
    return Status::invalid_argument();
  }
  for (const float sample : samples) {
    if (!std::isfinite(sample)) {
      return Status::invalid_argument();
    }
  }

  for (const float sample : samples) {
    const std::size_t ring_index =
        static_cast<std::size_t>(total_canonical_samples_ % kWindowSize);
    ring_[ring_index] = sample;
    ++total_canonical_samples_;

    if (total_canonical_samples_ == next_frame_start_ + kWindowSize) {
      const Status status = emit(sink);
      if (!status.ok()) {
        failed_ = true;
        return status;
      }
    }
  }
  return Status::ok_status();
}

Status FrameStream::emit(AnalysisFrameSink& sink) noexcept {
  AnalysisFrame frame{
      .time_frame = emitted_frames_,
      .first_canonical_sample = next_frame_start_,
      .samples = {},
  };

  for (std::size_t index = 0; index < kWindowSize; ++index) {
    const std::uint64_t canonical_index = next_frame_start_ + index;
    const std::size_t ring_index = static_cast<std::size_t>(canonical_index % kWindowSize);
    frame.samples[index] = static_cast<float>(
        static_cast<double>(ring_[ring_index]) *
        (static_cast<double>(kHannQ31[index]) / kQ31Scale));
  }

  const Status status = sink.consume(frame);
  if (!status.ok()) {
    return status;
  }
  ++emitted_frames_;
  next_frame_start_ += kHopSize;
  return Status::ok_status();
}

Status FrameStream::finish() noexcept {
  if (finished_ || failed_) {
    return Status::invalid_state();
  }
  finished_ = true;
  return Status::ok_status();
}

void FrameStream::reset() noexcept {
  std::fill(ring_.begin(), ring_.end(), 0.0F);
  total_canonical_samples_ = 0;
  next_frame_start_ = 0;
  emitted_frames_ = 0;
  finished_ = false;
  failed_ = false;
}

std::uint64_t FrameStream::total_canonical_samples() const noexcept {
  return total_canonical_samples_;
}

std::uint64_t FrameStream::emitted_frames() const noexcept {
  return emitted_frames_;
}

}  // namespace local_acr
