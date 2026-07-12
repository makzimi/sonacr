#ifndef LOCAL_ACR_FINGERPRINT_FINGERPRINTER_HPP
#define LOCAL_ACR_FINGERPRINT_FINGERPRINTER_HPP

#include "audio/frame_stream.hpp"
#include "audio/pcm_ingress.hpp"
#include "audio/resampler.hpp"
#include "fingerprint/landmark_builder.hpp"
#include "fingerprint/peak_selector.hpp"
#include "fingerprint/spectrum.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>

namespace local_acr {

class Fingerprinter final : private MonoSink,
                            private CanonicalSampleSink,
                            private AnalysisFrameSink,
                            private PeakSink {
 public:
  static constexpr std::size_t kMaxPcmFramesPerPush = 4096;
  static constexpr std::size_t kResamplerScratchSamples = 4096;

  explicit Fingerprinter(std::uint32_t input_sample_rate) noexcept;
  ~Fingerprinter() override = default;

  Fingerprinter(const Fingerprinter&) = delete;
  Fingerprinter& operator=(const Fingerprinter&) = delete;

  [[nodiscard]] bool valid() const noexcept;
  [[nodiscard]] Status push(const PcmView& pcm, LandmarkSink& sink) noexcept;
  [[nodiscard]] Status finish_finite(LandmarkSink& sink) noexcept;
  void reset() noexcept;

 private:
  Status consume(MonoPcmView pcm) noexcept override;
  Status consume(std::span<const float> samples) noexcept override;
  Status consume(const AnalysisFrame& frame) noexcept override;
  Status consume(std::span<const ConfirmedPeak> peaks) noexcept override;

  std::uint32_t input_sample_rate_ = 0;
  std::array<float, kMaxPcmFramesPerPush> mono_scratch_{};
  std::array<float, kResamplerScratchSamples> resampler_scratch_{};
  PcmIngress pcm_ingress_;
  std::optional<CanonicalResampler> resampler_;
  FrameStream frame_stream_;
  SpectrumAnalyzer spectrum_;
  PeakSelector peak_selector_;
  LandmarkBuilder landmark_builder_;
  LandmarkSink* active_sink_ = nullptr;
};

}  // namespace local_acr

#endif
