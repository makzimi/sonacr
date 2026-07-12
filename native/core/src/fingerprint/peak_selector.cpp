#include "fingerprint/peak_selector.hpp"

#include "fingerprint/gaussian_penalties_q16.hpp"
#include "fingerprint/log_q16.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace local_acr {

namespace {

constexpr std::int32_t kNegativeInfinity = std::numeric_limits<std::int32_t>::min();

}  // namespace

Status PeakSelector::process(const DecisionSpectrum& spectrum, PeakSink& sink) noexcept {
  if (spectrum.time_frame != processed_frames_) {
    return Status::audio_discontinuity();
  }

  if (!threshold_initialized_) {
    if (processed_frames_ == 0) {
      warmup_max_ = spectrum.q16;
    } else {
      for (std::size_t index = 0; index < kBinCount; ++index) {
        warmup_max_[index] = std::max(warmup_max_[index], spectrum.q16[index]);
      }
    }
    ++processed_frames_;
    if (processed_frames_ == kWarmupFrames) {
      const Status status = initialize_threshold_after_warmup();
      if (!status.ok()) {
        return status;
      }
      decay_thresholds();
    }
    return Status::ok_status();
  }

  const Status status = process_candidates(spectrum, sink);
  if (!status.ok()) {
    return status;
  }
  ++processed_frames_;
  return Status::ok_status();
}

Status PeakSelector::finish_finite(PeakSink& sink) noexcept {
  (void)sink;
  provisional_count_ = 0;
  return Status::ok_status();
}

void PeakSelector::reset() noexcept {
  warmup_max_.fill(0);
  threshold_.fill(0);
  provisional_count_ = 0;
  processed_frames_ = 0;
  threshold_initialized_ = false;
}

std::uint64_t PeakSelector::processed_frames() const noexcept {
  return processed_frames_;
}

std::size_t PeakSelector::provisional_count() const noexcept {
  return provisional_count_;
}

Status PeakSelector::initialize_threshold_after_warmup() noexcept {
  for (std::size_t target = 0; target < kBinCount; ++target) {
    std::int32_t best = kNegativeInfinity;
    for (std::size_t source = 0; source < kBinCount; ++source) {
      best = std::max(best, saturating_add_q16(warmup_max_[source],
                                               kGaussianPenaltiesQ16[source][target]));
    }
    threshold_[target] = best;
  }
  threshold_initialized_ = true;
  return Status::ok_status();
}

Status PeakSelector::process_candidates(const DecisionSpectrum& spectrum, PeakSink& sink) noexcept {
  std::array<Candidate, kMaxCandidatesPerFrame> candidates{};
  std::size_t candidate_count = 0;
  select_candidates(spectrum, candidates, candidate_count);
  const std::span<const Candidate> selected{candidates.data(), candidate_count};
  update_thresholds_and_suppress(selected);

  for (const Candidate& candidate : selected) {
    const Status status = append_provisional(candidate);
    if (!status.ok()) {
      return status;
    }
  }

  const Status status = confirm_expired(spectrum.time_frame, sink);
  if (!status.ok()) {
    return status;
  }
  decay_thresholds();
  return Status::ok_status();
}

void PeakSelector::decay_thresholds() noexcept {
  for (std::int32_t& value : threshold_) {
    value = saturating_add_q16(value, kThresholdDecayQ16);
  }
}

void PeakSelector::select_candidates(const DecisionSpectrum& spectrum,
                                     std::array<Candidate, kMaxCandidatesPerFrame>& candidates,
                                     std::size_t& candidate_count) const noexcept {
  std::array<Candidate, kBinCount> all_candidates{};
  std::size_t all_count = 0;

  for (std::size_t index = 0; index < kBinCount; ++index) {
    const std::int32_t value = spectrum.q16[index];
    const std::int32_t left = index == 0 ? kNegativeInfinity : spectrum.q16[index - 1U];
    const std::int32_t right =
        index + 1U == kBinCount ? kNegativeInfinity : spectrum.q16[index + 1U];
    if (value > threshold_[index] && value > left && value >= right) {
      all_candidates[all_count++] =
          Candidate{spectrum.time_frame, static_cast<std::uint8_t>(index + 1U), value};
    }
  }

  std::sort(all_candidates.begin(), all_candidates.begin() + static_cast<std::ptrdiff_t>(all_count),
            [](const Candidate& lhs, const Candidate& rhs) {
              if (lhs.value_q16 != rhs.value_q16) {
                return lhs.value_q16 > rhs.value_q16;
              }
              return lhs.bin < rhs.bin;
            });

  candidate_count = std::min(all_count, kMaxCandidatesPerFrame);
  for (std::size_t index = 0; index < candidate_count; ++index) {
    candidates[index] = all_candidates[index];
  }
}

void PeakSelector::update_thresholds_and_suppress(std::span<const Candidate> candidates) noexcept {
  for (const Candidate& candidate : candidates) {
    const std::size_t source = static_cast<std::size_t>(candidate.bin - 1U);
    for (std::size_t target = 0; target < kBinCount; ++target) {
      const std::int32_t masked =
          saturating_add_q16(candidate.value_q16, kGaussianPenaltiesQ16[source][target]);
      threshold_[target] = std::max(threshold_[target], masked);
    }

    for (std::size_t index = 0; index < provisional_count_; ++index) {
      ProvisionalPeak& older = provisional_[index];
      if (!older.suppressed &&
          older.peak.value_q16 <= decayed_suppression_value(candidate, older.peak)) {
        older.suppressed = true;
      }
    }
  }
}

std::int32_t PeakSelector::decayed_suppression_value(const Candidate& newer,
                                                     const Candidate& older) const noexcept {
  const std::size_t newer_bin = static_cast<std::size_t>(newer.bin - 1U);
  const std::size_t older_bin = static_cast<std::size_t>(older.bin - 1U);
  const std::int32_t masked =
      saturating_add_q16(newer.value_q16, kGaussianPenaltiesQ16[newer_bin][older_bin]);
  const std::uint64_t delta_time = newer.time_frame - older.time_frame;
  std::int32_t decayed = masked;
  for (std::uint64_t i = 0; i < delta_time; ++i) {
    decayed = saturating_add_q16(decayed, kThresholdDecayQ16);
  }
  return decayed;
}

Status PeakSelector::append_provisional(const Candidate& candidate) noexcept {
  if (provisional_count_ >= kMaxProvisionalPeaks) {
    return Status::resource_limit_exceeded();
  }
  provisional_[provisional_count_++] = ProvisionalPeak{candidate, false};
  return Status::ok_status();
}

Status PeakSelector::confirm_expired(std::uint64_t time_frame, PeakSink& sink) noexcept {
  std::size_t emit_count = 0;
  for (std::size_t index = 0; index < provisional_count_; ++index) {
    const ProvisionalPeak& provisional = provisional_[index];
    if (time_frame >= provisional.peak.time_frame &&
        time_frame - provisional.peak.time_frame == kConfirmationDelayFrames &&
        !provisional.suppressed) {
      if (emit_count >= emit_buffer_.size()) {
        return Status::resource_limit_exceeded();
      }
      emit_buffer_[emit_count++] =
          ConfirmedPeak{static_cast<std::uint32_t>(provisional.peak.time_frame),
                        provisional.peak.bin, provisional.peak.value_q16};
    }
  }

  if (emit_count > 0) {
    std::sort(emit_buffer_.begin(), emit_buffer_.begin() + static_cast<std::ptrdiff_t>(emit_count),
              [](const ConfirmedPeak& lhs, const ConfirmedPeak& rhs) {
                if (lhs.time_frame != rhs.time_frame) {
                  return lhs.time_frame < rhs.time_frame;
                }
                return lhs.bin < rhs.bin;
              });
    const Status status = sink.consume(std::span<const ConfirmedPeak>{emit_buffer_.data(), emit_count});
    if (!status.ok()) {
      return status;
    }
  }

  compact_expired(time_frame);
  return Status::ok_status();
}

void PeakSelector::compact_expired(std::uint64_t time_frame) noexcept {
  std::size_t write = 0;
  for (std::size_t read = 0; read < provisional_count_; ++read) {
    const ProvisionalPeak& provisional = provisional_[read];
    const bool expired = time_frame >= provisional.peak.time_frame &&
                         time_frame - provisional.peak.time_frame >= kConfirmationDelayFrames;
    if (!expired) {
      provisional_[write++] = provisional;
    }
  }
  provisional_count_ = write;
}

}  // namespace local_acr
