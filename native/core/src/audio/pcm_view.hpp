#ifndef LOCAL_ACR_AUDIO_PCM_VIEW_HPP
#define LOCAL_ACR_AUDIO_PCM_VIEW_HPP

#include <cstddef>
#include <cstdint>
#include <span>

namespace local_acr {

enum class SampleFormat : std::uint8_t {
  S16Interleaved = 1,
  F32Interleaved = 2,
  F32Planar = 3,
};

struct PcmPlane final {
  const void* data;
  std::size_t byte_count;
};

struct PcmView final {
  std::span<const PcmPlane> planes;
  std::uint32_t frames;
  std::uint32_t channels;
  std::uint32_t sample_rate;
  std::uint64_t first_source_frame;
  SampleFormat format;
};

struct MonoPcmView final {
  std::span<const float> samples;
  std::uint64_t first_source_frame;
  std::uint32_t sample_rate;
};

}  // namespace local_acr

#endif
