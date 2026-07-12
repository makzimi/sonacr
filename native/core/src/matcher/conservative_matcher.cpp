#include "matcher/conservative_matcher.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

namespace local_acr {

namespace {

struct WinnerSelection final {
  const AlignedCandidate* winner = nullptr;
  std::string runner_up_trigger_id;
  std::uint32_t runner_up_count = 0;
  double runner_up_ratio = 0.0;
  std::optional<std::uint32_t> ambiguous_secondary_count;
};

bool winner_less(const AlignedCandidate& lhs, const AlignedCandidate& rhs) {
  if (lhs.aligned_count != rhs.aligned_count) {
    return lhs.aligned_count > rhs.aligned_count;
  }
  if (lhs.aligned_ratio != rhs.aligned_ratio) {
    return lhs.aligned_ratio > rhs.aligned_ratio;
  }
  return lhs.trigger_id < rhs.trigger_id;
}

bool is_disjoint_secondary(const AlignedCandidate& winner, const AlignedCandidate& candidate) noexcept {
  if (winner.trigger_id != candidate.trigger_id) {
    return false;
  }
  const std::int64_t delta = candidate.center_bucket - winner.center_bucket;
  const std::int64_t distance = delta < 0 ? -delta : delta;
  return distance >= 3;
}

WinnerSelection select_winner(const std::vector<AlignedCandidate>& candidates,
                              const MatcherProfile& profile) noexcept {
  WinnerSelection selection;
  for (const AlignedCandidate& candidate : candidates) {
    if (selection.winner == nullptr || winner_less(candidate, *selection.winner)) {
      selection.winner = &candidate;
    }
  }
  if (selection.winner == nullptr) {
    return selection;
  }

  for (const AlignedCandidate& candidate : candidates) {
    if (&candidate == selection.winner) {
      continue;
    }
    if (candidate.trigger_id != selection.winner->trigger_id) {
      if (candidate.aligned_count > selection.runner_up_count ||
          (candidate.aligned_count == selection.runner_up_count &&
           candidate.aligned_ratio > selection.runner_up_ratio)) {
        selection.runner_up_count = candidate.aligned_count;
        selection.runner_up_ratio = candidate.aligned_ratio;
        selection.runner_up_trigger_id = candidate.trigger_id;
      }
    } else if (is_disjoint_secondary(*selection.winner, candidate)) {
      const double secondary_ratio =
          selection.winner->aligned_count == 0U
              ? 0.0
              : static_cast<double>(candidate.aligned_count) /
                    static_cast<double>(selection.winner->aligned_count);
      if (secondary_ratio >= profile.ambiguous_secondary_ratio) {
        if (!selection.ambiguous_secondary_count.has_value() ||
            candidate.aligned_count > *selection.ambiguous_secondary_count) {
          selection.ambiguous_secondary_count = candidate.aligned_count;
        }
      }
    }
  }
  return selection;
}

MatcherResult reject(const WinnerSelection& selection, MatcherRejection rejection) noexcept {
  MatcherResult result;
  result.diagnostics.rejection = rejection;
  if (selection.winner != nullptr) {
    result.diagnostics.winner_trigger_id = selection.winner->trigger_id;
    result.diagnostics.winner_count = selection.winner->aligned_count;
    result.diagnostics.winner_ratio = selection.winner->aligned_ratio;
    result.diagnostics.winner_center_bucket = selection.winner->center_bucket;
  }
  result.diagnostics.runner_up_count = selection.runner_up_count;
  result.diagnostics.runner_up_ratio = selection.runner_up_ratio;
  result.diagnostics.runner_up_trigger_id = selection.runner_up_trigger_id;
  return result;
}

std::optional<std::uint32_t> trigger_duration(const MatcherEvaluation& evaluation,
                                              const std::string& trigger_id) noexcept {
  for (const TriggerCueDuration& duration : evaluation.trigger_durations) {
    if (duration.trigger_id == trigger_id) {
      return duration.duration_frames;
    }
  }
  return std::nullopt;
}

double clamp01(const double value) noexcept {
  if (value < 0.0) {
    return 0.0;
  }
  if (value > 1.0) {
    return 1.0;
  }
  return value;
}

double confidence(const AlignedCandidate& winner, const std::uint32_t runner_up_count) noexcept {
  const double evidence = std::min(1.0, static_cast<double>(winner.aligned_count) / 24.0);
  const double coverage = std::min(1.0, winner.aligned_ratio / 0.30);
  const double separation =
      runner_up_count == 0U
          ? 1.0
          : clamp01(1.0 - (static_cast<double>(runner_up_count) /
                           static_cast<double>(winner.aligned_count)));
  return (0.45 * evidence) + (0.35 * coverage) + (0.20 * separation);
}

}  // namespace

MatcherResult ConservativeMatcher::evaluate(const MatcherEvaluation& evaluation) const noexcept {
  const MatcherProfile& active_profile = profile();
  const WinnerSelection selection = select_winner(evaluation.candidates, active_profile);
  if (selection.winner == nullptr) {
    return reject(selection, MatcherRejection::NoCandidates);
  }

  if (selection.ambiguous_secondary_count.has_value()) {
    return reject(selection, MatcherRejection::AmbiguousOffset);
  }
  if (selection.winner->aligned_count < active_profile.minimum_aligned_landmarks) {
    return reject(selection, MatcherRejection::InsufficientEvidence);
  }
  if (selection.winner->aligned_ratio < active_profile.minimum_aligned_ratio) {
    return reject(selection, MatcherRejection::InsufficientCoverage);
  }
  if (selection.winner->aligned_count <
      selection.runner_up_count + active_profile.minimum_winner_margin) {
    return reject(selection, MatcherRejection::InsufficientMargin);
  }
  if (selection.runner_up_count != 0U &&
      static_cast<double>(selection.winner->aligned_count) /
              static_cast<double>(selection.runner_up_count) <
          active_profile.minimum_runner_up_ratio) {
    return reject(selection, MatcherRejection::InsufficientRunnerUpRatio);
  }
  if (!evaluation.previous_winner.has_value() ||
      evaluation.previous_winner->trigger_id != selection.winner->trigger_id) {
    return reject(selection, MatcherRejection::NotConsecutiveWinner);
  }

  const std::int64_t offset_delta =
      selection.winner->center_bucket - evaluation.previous_winner->center_bucket;
  const std::int64_t offset_distance = offset_delta < 0 ? -offset_delta : offset_delta;
  if (offset_distance > active_profile.maximum_offset_bucket_delta) {
    return reject(selection, MatcherRejection::OffsetUnstable);
  }

  const std::int64_t matched_frame =
      static_cast<std::int64_t>(evaluation.newest_query_time_frame) +
      (selection.winner->center_bucket * 2);
  const std::optional<std::uint32_t> duration = trigger_duration(evaluation, selection.winner->trigger_id);
  if (!duration.has_value() || matched_frame < 0 ||
      matched_frame >= static_cast<std::int64_t>(*duration)) {
    return reject(selection, MatcherRejection::MatchedPositionOutOfRange);
  }

  MatcherResult result = reject(selection, MatcherRejection::None);
  result.recognized = NativeRecognitionResult{
      .trigger_id = selection.winner->trigger_id,
      .confidence = confidence(*selection.winner, selection.runner_up_count),
      .matched_cue_time_frame = static_cast<std::uint32_t>(matched_frame),
      .newest_source_frame = evaluation.newest_source_frame,
      .aligned_count = selection.winner->aligned_count,
      .aligned_ratio = selection.winner->aligned_ratio,
  };
  return result;
}

}  // namespace local_acr
