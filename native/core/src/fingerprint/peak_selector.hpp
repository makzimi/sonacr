#ifndef LOCAL_ACR_FINGERPRINT_PEAK_SELECTOR_HPP
#define LOCAL_ACR_FINGERPRINT_PEAK_SELECTOR_HPP

#include "fingerprint/temporal_filter.hpp"
#include "support/status.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace local_acr {

struct ConfirmedPeak final {
  std::uint32_t time_frame = 0;
  std::uint8_t bin = 0;
  std::int32_t value_q16 = 0;

  [[nodiscard]] friend constexpr bool operator==(const ConfirmedPeak& lhs,
                                                 const ConfirmedPeak& rhs) noexcept = default;
};

class PeakSink {
 public:
  virtual ~PeakSink() = default;
  virtual Status consume(std::span<const ConfirmedPeak> confirmed) noexcept = 0;
};

class PeakSelector final {
 public:
  static constexpr std::size_t kBinCount = 255;
  static constexpr std::uint64_t kWarmupFrames = 10;
  static constexpr std::uint64_t kConfirmationDelayFrames = 22;
  static constexpr std::size_t kMaxCandidatesPerFrame = 5;
  static constexpr std::size_t kMaxProvisionalPeaks =
      kMaxCandidatesPerFrame * (kConfirmationDelayFrames + 1);

  [[nodiscard]] Status process(const DecisionSpectrum& spectrum, PeakSink& sink) noexcept;
  [[nodiscard]] Status finish_finite(PeakSink& sink) noexcept;
  void reset() noexcept;

  [[nodiscard]] std::uint64_t processed_frames() const noexcept;
  [[nodiscard]] std::size_t provisional_count() const noexcept;

 private:
  struct Candidate final {
    std::uint64_t time_frame = 0;
    std::uint8_t bin = 0;
    std::int32_t value_q16 = 0;
  };

  struct ProvisionalPeak final {
    Candidate peak{};
    bool suppressed = false;
  };

  [[nodiscard]] Status initialize_threshold_after_warmup() noexcept;
  [[nodiscard]] Status process_candidates(const DecisionSpectrum& spectrum, PeakSink& sink) noexcept;
  void decay_thresholds() noexcept;
  void select_candidates(const DecisionSpectrum& spectrum,
                         std::array<Candidate, kMaxCandidatesPerFrame>& candidates,
                         std::size_t& candidate_count) const noexcept;
  void update_thresholds_and_suppress(std::span<const Candidate> candidates) noexcept;
  [[nodiscard]] std::int32_t decayed_suppression_value(const Candidate& newer,
                                                       const Candidate& older) const noexcept;
  [[nodiscard]] Status append_provisional(const Candidate& candidate) noexcept;
  [[nodiscard]] Status confirm_expired(std::uint64_t time_frame, PeakSink& sink) noexcept;
  void compact_expired(std::uint64_t time_frame) noexcept;

  std::array<std::int32_t, kBinCount> warmup_max_{};
  std::array<std::int32_t, kBinCount> threshold_{};
  std::array<ProvisionalPeak, kMaxProvisionalPeaks> provisional_{};
  std::array<ConfirmedPeak, kMaxCandidatesPerFrame> emit_buffer_{};
  std::uint64_t processed_frames_ = 0;
  std::size_t provisional_count_ = 0;
  bool threshold_initialized_ = false;
};

}  // namespace local_acr

#endif
