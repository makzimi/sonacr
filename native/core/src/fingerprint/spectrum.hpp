#ifndef LOCAL_ACR_FINGERPRINT_SPECTRUM_HPP
#define LOCAL_ACR_FINGERPRINT_SPECTRUM_HPP

#include "audio/frame_stream.hpp"
#include "fingerprint/temporal_filter.hpp"
#include "support/status.hpp"

#include <array>
#include <cstdint>

struct kiss_fftr_state;

namespace local_acr {

class SpectrumAnalyzer final {
 public:
  static constexpr std::size_t kWindowSize = 512;
  static constexpr std::size_t kBinCount = 255;

  SpectrumAnalyzer() noexcept;
  ~SpectrumAnalyzer();

  SpectrumAnalyzer(const SpectrumAnalyzer&) = delete;
  SpectrumAnalyzer& operator=(const SpectrumAnalyzer&) = delete;

  [[nodiscard]] Status analyze_log(const AnalysisFrame& frame, LogSpectrum& output) noexcept;
  [[nodiscard]] LogSpectrum analyze_log(const AnalysisFrame& frame) noexcept;
  [[nodiscard]] Status process(const AnalysisFrame& frame, DecisionSpectrum& output) noexcept;
  void reset() noexcept;

 private:
  struct ComplexBin final {
    float r = 0.0F;
    float i = 0.0F;
  };

  kiss_fftr_state* fft_cfg_ = nullptr;
  std::array<float, kWindowSize> time_buffer_{};
  std::array<ComplexBin, 257> freq_buffer_{};
  TemporalFilter temporal_filter_{};
};

}  // namespace local_acr

#endif
