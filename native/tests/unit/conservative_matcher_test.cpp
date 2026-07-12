#include "matcher/conservative_matcher.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

bool near(double actual, double expected) {
  return std::fabs(actual - expected) < 0.000001;
}

std::vector<std::uint32_t> ids(std::uint32_t count) {
  std::vector<std::uint32_t> values;
  values.reserve(count);
  for (std::uint32_t i = 0; i < count; ++i) {
    values.push_back(i);
  }
  return values;
}

local_acr::AlignedCandidate candidate(std::string_view trigger_id,
                                      std::int64_t center_bucket,
                                      std::uint32_t aligned_count,
                                      std::uint32_t query_count) {
  return local_acr::AlignedCandidate{
      .trigger_id = std::string(trigger_id),
      .center_bucket = center_bucket,
      .aligned_query_ids = ids(aligned_count),
      .aligned_count = aligned_count,
      .aligned_ratio = static_cast<double>(aligned_count) / static_cast<double>(query_count),
  };
}

local_acr::TriggerCueDuration duration(std::string_view trigger_id, std::uint32_t frames) {
  return local_acr::TriggerCueDuration{.trigger_id = std::string(trigger_id), .duration_frames = frames};
}

local_acr::MatcherEvaluation evaluation(
    std::vector<local_acr::AlignedCandidate> candidates,
    std::optional<local_acr::PreviousWinner> previous = local_acr::PreviousWinner{
        .trigger_id = "winner",
        .center_bucket = 10,
    },
    std::uint32_t query_count = 100,
    std::uint32_t newest_query_time_frame = 100,
    std::uint64_t newest_source_frame = 48000,
    std::vector<local_acr::TriggerCueDuration> durations = {duration("winner", 1000),
                                                            duration("runner", 1000),
                                                            duration("zzz", 1000)}) {
  return local_acr::MatcherEvaluation{
      .candidates = std::move(candidates),
      .previous_winner = std::move(previous),
      .unique_query_landmark_count = query_count,
      .newest_query_time_frame = newest_query_time_frame,
      .newest_source_frame = newest_source_frame,
      .trigger_durations = std::move(durations),
  };
}

void evidence_and_coverage_boundaries() {
  local_acr::ConservativeMatcher matcher;

  auto result = matcher.evaluate(evaluation({candidate("winner", 10, 11, 100)}));
  check(!result.recognized.has_value(), "aligned count 11 rejects");
  check(result.diagnostics.rejection == local_acr::MatcherRejection::InsufficientEvidence,
        "aligned count diagnostic");

  result = matcher.evaluate(evaluation({candidate("winner", 10, 12, 101)}, local_acr::PreviousWinner{
                                                                            .trigger_id = "winner",
                                                                            .center_bucket = 10,
                                                                        },
                                      101));
  check(!result.recognized.has_value(), "ratio below 12 percent rejects");
  check(result.diagnostics.rejection == local_acr::MatcherRejection::InsufficientCoverage,
        "coverage diagnostic");

  result = matcher.evaluate(evaluation({candidate("winner", 10, 12, 100)}));
  check(result.recognized.has_value(), "aligned count 12 and ratio 12 percent accept");
}

void runner_up_margin_and_ratio_boundaries() {
  local_acr::ConservativeMatcher matcher;

  auto result = matcher.evaluate(evaluation({candidate("winner", 10, 12, 100),
                                             candidate("runner", 10, 8, 100)}));
  check(!result.recognized.has_value(), "winner margin 4 rejects");
  check(result.diagnostics.rejection == local_acr::MatcherRejection::InsufficientMargin,
        "margin diagnostic");

  result = matcher.evaluate(evaluation({candidate("winner", 10, 12, 100),
                                        candidate("runner", 10, 7, 100)}));
  check(result.recognized.has_value(), "winner margin 5 accepts");

  result = matcher.evaluate(evaluation({candidate("winner", 10, 30, 100),
                                        candidate("runner", 10, 25, 100)}));
  check(!result.recognized.has_value(), "runner-up ratio below 1.25 rejects even when margin gate passes");
  check(result.diagnostics.rejection == local_acr::MatcherRejection::InsufficientRunnerUpRatio,
        "runner-up ratio diagnostic");

  result = matcher.evaluate(evaluation({candidate("winner", 10, 25, 100),
                                        candidate("runner", 10, 20, 100)}));
  check(result.recognized.has_value(), "runner-up ratio exactly 1.25 accepts");

  result = matcher.evaluate(evaluation({candidate("winner", 10, 12, 100)}));
  check(result.recognized.has_value(), "zero runner-up accepts when other gates hold");
}

void ambiguity_consecutive_and_offset_stability_gates() {
  local_acr::ConservativeMatcher matcher;

  auto result = matcher.evaluate(evaluation({candidate("winner", 10, 20, 100),
                                             candidate("winner", 13, 16, 100)}));
  check(!result.recognized.has_value(), "secondary at 80 percent rejects as ambiguous");
  check(result.diagnostics.rejection == local_acr::MatcherRejection::AmbiguousOffset,
        "ambiguity diagnostic");

  result = matcher.evaluate(evaluation({candidate("winner", 10, 20, 100)},
                                       local_acr::PreviousWinner{.trigger_id = "other", .center_bucket = 10}));
  check(!result.recognized.has_value(), "different previous trigger rejects");
  check(result.diagnostics.rejection == local_acr::MatcherRejection::NotConsecutiveWinner,
        "consecutive trigger diagnostic");

  result = matcher.evaluate(evaluation({candidate("winner", 11, 20, 100)},
                                       local_acr::PreviousWinner{.trigger_id = "winner", .center_bucket = 10}));
  check(result.recognized.has_value(), "one-bucket offset movement accepts");

  result = matcher.evaluate(evaluation({candidate("winner", 12, 20, 100)},
                                       local_acr::PreviousWinner{.trigger_id = "winner", .center_bucket = 10}));
  check(!result.recognized.has_value(), "two-bucket offset movement rejects");
  check(result.diagnostics.rejection == local_acr::MatcherRejection::OffsetUnstable,
        "offset stability diagnostic");
}

void deterministic_trigger_tie_and_confidence_formula() {
  local_acr::ConservativeMatcher matcher;

  auto result = matcher.evaluate(evaluation({candidate("zzz", 10, 24, 100),
                                             candidate("winner", 10, 24, 100)}));
  check(!result.recognized.has_value(), "different-trigger tied winner is rejected by separation gates");
  check(result.diagnostics.winner_trigger_id == "winner", "bytewise trigger id breaks winner tie");
  check(result.diagnostics.runner_up_trigger_id == "zzz", "runner-up trigger is tracked");
  check(result.diagnostics.winner_count == 24, "deterministic tie still selects a stable winner count");

  result = matcher.evaluate(evaluation({candidate("winner", 10, 12, 100)}));
  check(result.recognized.has_value(), "confidence weighted fixture recognizes");
  if (result.recognized.has_value()) {
    check(near(result.recognized->confidence, 0.45 * 0.5 + 0.35 * (0.12 / 0.30) + 0.20),
          "confidence uses evidence coverage separation formula");
  }

  result = matcher.evaluate(evaluation({candidate("winner", 10, 24, 80)}, local_acr::PreviousWinner{
                                                                              .trigger_id = "winner",
                                                                              .center_bucket = 10,
                                                                          },
                                      80));
  check(result.recognized.has_value(), "confidence endpoint fixture recognizes");
  if (result.recognized.has_value()) {
    check(near(result.recognized->confidence, 1.0), "confidence reaches one at evidence/coverage/separation caps");
  }
}

void matched_position_and_range_validation() {
  local_acr::ConservativeMatcher matcher;

  auto result = matcher.evaluate(evaluation({candidate("winner", 10, 12, 100)}));
  check(result.recognized.has_value(), "position fixture recognizes");
  if (result.recognized.has_value()) {
    check(result.recognized->matched_cue_time_frame == 120, "center bucket maps newest query to cue frame");
    check(result.recognized->newest_source_frame == 48000, "newest source frame is carried through");
  }

  result = matcher.evaluate(evaluation({candidate("winner", -51, 12, 100)},
                                       local_acr::PreviousWinner{.trigger_id = "winner", .center_bucket = -51}));
  check(!result.recognized.has_value(), "negative matched cue frame rejects");
  check(result.diagnostics.rejection == local_acr::MatcherRejection::MatchedPositionOutOfRange,
        "negative position diagnostic");

  result = matcher.evaluate(evaluation({candidate("winner", 500, 12, 100)},
                                       local_acr::PreviousWinner{.trigger_id = "winner", .center_bucket = 500}));
  check(!result.recognized.has_value(), "matched cue frame beyond duration rejects");
  check(result.diagnostics.rejection == local_acr::MatcherRejection::MatchedPositionOutOfRange,
        "over-duration position diagnostic");
}

}  // namespace

int main() {
  evidence_and_coverage_boundaries();
  runner_up_margin_and_ratio_boundaries();
  ambiguity_consecutive_and_offset_stability_gates();
  deterministic_trigger_tie_and_confidence_formula();
  matched_position_and_range_validation();
  return failures == 0 ? 0 : 1;
}
