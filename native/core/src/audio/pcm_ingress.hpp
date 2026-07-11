#ifndef LOCAL_ACR_AUDIO_PCM_INGRESS_HPP
#define LOCAL_ACR_AUDIO_PCM_INGRESS_HPP

#include "audio/pcm_view.hpp"
#include "support/status.hpp"

#include <cstdint>
#include <span>

namespace local_acr {

class MonoSink {
 public:
  virtual ~MonoSink() = default;
  virtual Status consume(MonoPcmView pcm) noexcept = 0;
};

class PcmIngress final {
 public:
  explicit PcmIngress(std::span<float> mono_scratch) noexcept;

  [[nodiscard]] Status push(const PcmView& pcm, MonoSink& sink) noexcept;
  void reset() noexcept;

 private:
  enum class State : std::uint8_t {
    Empty,
    Streaming,
    Discontinuous,
  };

  std::span<float> mono_scratch_;
  State state_ = State::Empty;
  std::uint32_t channels_ = 0;
  std::uint32_t sample_rate_ = 0;
  SampleFormat format_ = SampleFormat::S16Interleaved;
  std::uint64_t expected_source_frame_ = 0;
};

}  // namespace local_acr

#endif
