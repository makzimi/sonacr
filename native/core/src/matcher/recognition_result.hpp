#ifndef LOCAL_ACR_MATCHER_RECOGNITION_RESULT_HPP
#define LOCAL_ACR_MATCHER_RECOGNITION_RESULT_HPP

#include "matcher/offset_accumulator.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace local_acr {

struct PreviousWinner final {
  std::string trigger_id;
  std::int64_t center_bucket = 0;
};

struct TriggerCueDuration final {
  std::string trigger_id;
  std::uint32_t duration_frames = 0;
};

enum class MatcherRejection : std::uint8_t {
  None = 0,
  NoCandidates,
  AmbiguousOffset,
  InsufficientEvidence,
  InsufficientCoverage,
  InsufficientMargin,
  InsufficientRunnerUpRatio,
  NotConsecutiveWinner,
  OffsetUnstable,
  MatchedPositionOutOfRange,
};

struct MatcherDiagnostics final {
  MatcherRejection rejection = MatcherRejection::None;
  std::string winner_trigger_id;
  std::string runner_up_trigger_id;
  std::uint32_t winner_count = 0;
  std::uint32_t runner_up_count = 0;
  double winner_ratio = 0.0;
  double runner_up_ratio = 0.0;
  std::int64_t winner_center_bucket = 0;
};

struct NativeRecognitionResult final {
  std::string trigger_id;
  double confidence = 0.0;
  std::uint32_t matched_cue_time_frame = 0;
  std::uint64_t newest_source_frame = 0;
  std::uint32_t aligned_count = 0;
  double aligned_ratio = 0.0;
};

struct MatcherEvaluation final {
  std::vector<AlignedCandidate> candidates;
  std::optional<PreviousWinner> previous_winner;
  std::uint32_t unique_query_landmark_count = 0;
  std::uint32_t newest_query_time_frame = 0;
  std::uint64_t newest_source_frame = 0;
  std::vector<TriggerCueDuration> trigger_durations;
};

struct MatcherResult final {
  std::optional<NativeRecognitionResult> recognized;
  MatcherDiagnostics diagnostics;
};

}  // namespace local_acr

#endif
