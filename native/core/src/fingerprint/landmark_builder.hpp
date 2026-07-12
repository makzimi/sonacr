#ifndef LOCAL_ACR_FINGERPRINT_LANDMARK_BUILDER_HPP
#define LOCAL_ACR_FINGERPRINT_LANDMARK_BUILDER_HPP

#include "fingerprint/landmark.hpp"
#include "fingerprint/peak_selector.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace local_acr {

class LandmarkBuilder final {
 public:
  static constexpr std::uint32_t kMinimumDeltaTime = 4;
  static constexpr std::uint32_t kMaximumDeltaTime = 96;
  static constexpr std::uint8_t kMaximumBinDelta = 32;
  static constexpr std::size_t kTargetsPerAnchor = 3;
  static constexpr std::size_t kMaxRetainedPeaks = 512;

  [[nodiscard]] Status process(std::span<const ConfirmedPeak> peaks,
                               LandmarkSink& sink) noexcept;
  [[nodiscard]] Status finish_finite(LandmarkSink& sink) noexcept;
  void reset() noexcept;

  [[nodiscard]] std::size_t retained_peak_count() const noexcept;

 private:
  struct RetainedPeak final {
    ConfirmedPeak peak{};
    std::size_t emitted_targets = 0;
  };

  [[nodiscard]] Status validate_order(std::span<const ConfirmedPeak> peaks) const noexcept;
  [[nodiscard]] Status emit_for_target(const ConfirmedPeak& target,
                                       LandmarkSink& sink) noexcept;
  [[nodiscard]] Status append_peak(const ConfirmedPeak& peak) noexcept;
  void expire_before(std::uint32_t target_time_frame) noexcept;
  [[nodiscard]] bool has_emitted_identity(const Landmark& landmark) const noexcept;
  [[nodiscard]] Status remember_identity(const Landmark& landmark) noexcept;

  std::array<RetainedPeak, kMaxRetainedPeaks> retained_{};
  std::array<Landmark, kMaxRetainedPeaks * kTargetsPerAnchor> emitted_identities_{};
  std::array<Landmark, 1> emit_buffer_{};
  std::size_t retained_count_ = 0;
  std::size_t emitted_identity_count_ = 0;
  std::uint32_t last_time_frame_ = 0;
  std::uint8_t last_bin_ = 0;
  bool has_last_peak_ = false;
};

}  // namespace local_acr

#endif
