#include "audio/pcm_ingress.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace local_acr {
namespace {

constexpr std::uint32_t kMinimumSampleRate = 8000;
constexpr std::uint32_t kMaximumSampleRate = 192000;
constexpr std::uint32_t kMaximumChannels = 8;

template <typename Sample>
[[nodiscard]] Sample read_sample(const PcmPlane& plane, std::size_t index) noexcept {
  Sample sample{};
  const auto* bytes = static_cast<const std::byte*>(plane.data);
  std::memcpy(&sample, bytes + index * sizeof(Sample), sizeof(Sample));
  return sample;
}

[[nodiscard]] bool has_bytes(const PcmPlane& plane, std::size_t sample_count,
                             std::size_t sample_size) noexcept {
  if (plane.data == nullptr) {
    return false;
  }
  if (sample_count > std::numeric_limits<std::size_t>::max() / sample_size) {
    return false;
  }
  return plane.byte_count >= sample_count * sample_size;
}

[[nodiscard]] Status validate_shape(const PcmView& pcm, std::size_t scratch_size) noexcept {
  if (pcm.frames == 0 || pcm.channels == 0 || pcm.channels > kMaximumChannels ||
      pcm.sample_rate < kMinimumSampleRate || pcm.sample_rate > kMaximumSampleRate) {
    return Status::invalid_argument();
  }
  if (static_cast<std::size_t>(pcm.frames) > scratch_size) {
    return Status::resource_limit_exceeded();
  }
  if (pcm.first_source_frame >
      std::numeric_limits<std::uint64_t>::max() - static_cast<std::uint64_t>(pcm.frames)) {
    return Status::invalid_argument();
  }

  const std::size_t frames = static_cast<std::size_t>(pcm.frames);
  const std::size_t channels = static_cast<std::size_t>(pcm.channels);
  switch (pcm.format) {
    case SampleFormat::S16Interleaved:
      if (pcm.planes.size() != 1 ||
          !has_bytes(pcm.planes.front(), frames * channels, sizeof(std::int16_t))) {
        return Status::invalid_argument();
      }
      break;
    case SampleFormat::F32Interleaved:
      if (pcm.planes.size() != 1 || !has_bytes(pcm.planes.front(), frames * channels, sizeof(float))) {
        return Status::invalid_argument();
      }
      break;
    case SampleFormat::F32Planar:
      if (pcm.planes.size() != channels) {
        return Status::invalid_argument();
      }
      for (const PcmPlane& plane : pcm.planes) {
        if (!has_bytes(plane, frames, sizeof(float))) {
          return Status::invalid_argument();
        }
      }
      break;
    default:
      return Status::invalid_argument();
  }
  return Status::ok_status();
}

[[nodiscard]] double sample_at(const PcmView& pcm, std::size_t frame,
                               std::size_t channel) noexcept {
  const std::size_t channels = static_cast<std::size_t>(pcm.channels);
  switch (pcm.format) {
    case SampleFormat::S16Interleaved:
      return static_cast<double>(read_sample<std::int16_t>(pcm.planes.front(), frame * channels + channel)) /
             32768.0;
    case SampleFormat::F32Interleaved:
      return static_cast<double>(read_sample<float>(pcm.planes.front(), frame * channels + channel));
    case SampleFormat::F32Planar:
      return static_cast<double>(read_sample<float>(pcm.planes[channel], frame));
  }
  return std::numeric_limits<double>::quiet_NaN();
}

}  // namespace

PcmIngress::PcmIngress(std::span<float> mono_scratch) noexcept : mono_scratch_(mono_scratch) {}

Status PcmIngress::push(const PcmView& pcm, MonoSink& sink) noexcept {
  if (state_ == State::Discontinuous) {
    return Status::audio_discontinuity();
  }

  const Status shape_status = validate_shape(pcm, mono_scratch_.size());
  if (!shape_status.ok()) {
    return shape_status;
  }

  if (state_ == State::Streaming &&
      (pcm.channels != channels_ || pcm.sample_rate != sample_rate_ || pcm.format != format_ ||
       pcm.first_source_frame != expected_source_frame_)) {
    state_ = State::Discontinuous;
    return Status::audio_discontinuity();
  }

  const std::size_t frames = static_cast<std::size_t>(pcm.frames);
  const std::size_t channels = static_cast<std::size_t>(pcm.channels);
  const double reciprocal_channels = 1.0 / static_cast<double>(channels);
  for (std::size_t frame = 0; frame < frames; ++frame) {
    double sum = 0.0;
    for (std::size_t channel = 0; channel < channels; ++channel) {
      const double sample = sample_at(pcm, frame, channel);
      if (!std::isfinite(sample)) {
        return Status::invalid_argument();
      }
      sum += sample;
    }
    mono_scratch_[frame] = static_cast<float>(std::clamp(sum * reciprocal_channels, -1.0, 1.0));
  }

  const Status sink_status = sink.consume(
      MonoPcmView{mono_scratch_.first(frames), pcm.first_source_frame, pcm.sample_rate});
  if (!sink_status.ok()) {
    return sink_status;
  }

  state_ = State::Streaming;
  channels_ = pcm.channels;
  sample_rate_ = pcm.sample_rate;
  format_ = pcm.format;
  expected_source_frame_ = pcm.first_source_frame + static_cast<std::uint64_t>(pcm.frames);
  return Status::ok_status();
}

void PcmIngress::reset() noexcept {
  state_ = State::Empty;
  channels_ = 0;
  sample_rate_ = 0;
  format_ = SampleFormat::S16Interleaved;
  expected_source_frame_ = 0;
}

}  // namespace local_acr
