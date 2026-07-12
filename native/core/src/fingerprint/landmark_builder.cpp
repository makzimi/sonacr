#include "fingerprint/landmark_builder.hpp"

#include <algorithm>
#include <cstdlib>

namespace local_acr {

namespace {

[[nodiscard]] std::uint32_t make_hash(const std::uint8_t anchor_bin,
                                      const std::uint8_t target_bin,
                                      const std::uint32_t delta_time) noexcept {
  return static_cast<std::uint32_t>(anchor_bin) |
         (static_cast<std::uint32_t>(target_bin) << 8U) |
         (delta_time << 16U);
}

[[nodiscard]] bool pair_is_valid(const ConfirmedPeak& anchor,
                                 const ConfirmedPeak& target) noexcept {
  if (target.time_frame < anchor.time_frame) {
    return false;
  }
  const std::uint32_t delta_time = target.time_frame - anchor.time_frame;
  if (delta_time < LandmarkBuilder::kMinimumDeltaTime ||
      delta_time > LandmarkBuilder::kMaximumDeltaTime) {
    return false;
  }
  const int bin_delta = std::abs(static_cast<int>(target.bin) - static_cast<int>(anchor.bin));
  return bin_delta <= static_cast<int>(LandmarkBuilder::kMaximumBinDelta);
}

}  // namespace

Status LandmarkBuilder::process(std::span<const ConfirmedPeak> peaks,
                                LandmarkSink& sink) noexcept {
  const Status order_status = validate_order(peaks);
  if (!order_status.ok()) {
    return order_status;
  }

  for (const ConfirmedPeak& peak : peaks) {
    expire_before(peak.time_frame);
    const Status emit_status = emit_for_target(peak, sink);
    if (!emit_status.ok()) {
      return emit_status;
    }
    const Status append_status = append_peak(peak);
    if (!append_status.ok()) {
      return append_status;
    }
    last_time_frame_ = peak.time_frame;
    last_bin_ = peak.bin;
    has_last_peak_ = true;
  }

  return Status::ok_status();
}

Status LandmarkBuilder::finish_finite(LandmarkSink& sink) noexcept {
  (void)sink;
  retained_count_ = 0;
  return Status::ok_status();
}

void LandmarkBuilder::reset() noexcept {
  retained_count_ = 0;
  emitted_identity_count_ = 0;
  last_time_frame_ = 0;
  last_bin_ = 0;
  has_last_peak_ = false;
}

std::size_t LandmarkBuilder::retained_peak_count() const noexcept {
  return retained_count_;
}

Status LandmarkBuilder::validate_order(std::span<const ConfirmedPeak> peaks) const noexcept {
  std::uint32_t previous_time = last_time_frame_;
  std::uint8_t previous_bin = last_bin_;
  bool has_previous = has_last_peak_;

  for (const ConfirmedPeak& peak : peaks) {
    if (peak.bin == 0) {
      return Status::invalid_argument();
    }
    if (has_previous &&
        (peak.time_frame < previous_time ||
         (peak.time_frame == previous_time && peak.bin < previous_bin))) {
      return Status::audio_discontinuity();
    }
    previous_time = peak.time_frame;
    previous_bin = peak.bin;
    has_previous = true;
  }
  return Status::ok_status();
}

Status LandmarkBuilder::emit_for_target(const ConfirmedPeak& target,
                                        LandmarkSink& sink) noexcept {
  for (std::uint32_t delta = kMinimumDeltaTime; delta <= kMaximumDeltaTime; ++delta) {
    if (target.time_frame < delta) {
      continue;
    }
    const std::uint32_t anchor_time = target.time_frame - delta;
    for (std::size_t index = 0; index < retained_count_; ++index) {
      RetainedPeak& retained = retained_[index];
      const ConfirmedPeak& anchor = retained.peak;
      if (retained.emitted_targets >= kTargetsPerAnchor) {
        continue;
      }
      if (anchor.time_frame != anchor_time || !pair_is_valid(anchor, target)) {
        continue;
      }
      const Landmark landmark{make_hash(anchor.bin, target.bin, delta), anchor.time_frame};
      if (has_emitted_identity(landmark)) {
        continue;
      }
      const Status remember_status = remember_identity(landmark);
      if (!remember_status.ok()) {
        return remember_status;
      }
      emit_buffer_[0] = landmark;
      ++retained.emitted_targets;
      const Status sink_status = sink.consume(std::span<const Landmark>{emit_buffer_.data(), 1});
      if (!sink_status.ok()) {
        return sink_status;
      }
    }
  }
  return Status::ok_status();
}

Status LandmarkBuilder::append_peak(const ConfirmedPeak& peak) noexcept {
  if (retained_count_ >= retained_.size()) {
    return Status::resource_limit_exceeded();
  }
  retained_[retained_count_++] = RetainedPeak{peak, 0};
  return Status::ok_status();
}

void LandmarkBuilder::expire_before(const std::uint32_t target_time_frame) noexcept {
  std::size_t write = 0;
  for (std::size_t read = 0; read < retained_count_; ++read) {
    const ConfirmedPeak& peak = retained_[read].peak;
    if (target_time_frame <= peak.time_frame ||
        target_time_frame - peak.time_frame <= kMaximumDeltaTime) {
      retained_[write++] = retained_[read];
    }
  }
  retained_count_ = write;
}

bool LandmarkBuilder::has_emitted_identity(const Landmark& landmark) const noexcept {
  for (std::size_t index = 0; index < emitted_identity_count_; ++index) {
    if (emitted_identities_[index] == landmark) {
      return true;
    }
  }
  return false;
}

Status LandmarkBuilder::remember_identity(const Landmark& landmark) noexcept {
  if (emitted_identity_count_ >= emitted_identities_.size()) {
    return Status::resource_limit_exceeded();
  }
  emitted_identities_[emitted_identity_count_++] = landmark;
  return Status::ok_status();
}

}  // namespace local_acr
