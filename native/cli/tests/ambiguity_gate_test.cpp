#include "ambiguity_gate.hpp"

#include <cstdint>
#include <iostream>
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

local_acr::DatabaseTrigger trigger(std::string_view id) {
  return local_acr::DatabaseTrigger{
      .trigger_id = std::string(id),
      .display_name = std::string(id),
      .duration_ms = 4000,
      .metadata_json = "{}",
  };
}

local_acr::FingerprintRow row(std::string_view trigger_id, std::uint32_t hash, std::uint32_t frame) {
  return local_acr::FingerprintRow{
      .trigger_id = std::string(trigger_id),
      .landmark = local_acr::Landmark{.hash = hash, .anchor_time_frame = frame},
  };
}

std::vector<local_acr::FingerprintRow> matching_rows(std::string_view trigger_id,
                                                     std::uint32_t first_hash,
                                                     std::uint32_t first_frame,
                                                     std::uint32_t count) {
  std::vector<local_acr::FingerprintRow> rows;
  rows.reserve(count);
  for (std::uint32_t i = 0; i < count; ++i) {
    rows.push_back(row(trigger_id, first_hash + i, first_frame + (i * 4U)));
  }
  return rows;
}

local_acr::SemanticDatabase database(std::vector<local_acr::DatabaseTrigger> triggers,
                                     std::vector<local_acr::FingerprintRow> rows) {
  return local_acr::SemanticDatabase{
      .metadata =
          local_acr::DatabaseMetadata{
              .database_id = "ambiguity",
              .database_version = "1",
              .build_report_json = "{}",
              .decoder_version = "test",
          },
      .triggers = std::move(triggers),
      .fingerprints = std::move(rows),
  };
}

void self_matches_are_excluded() {
  std::vector<local_acr::FingerprintRow> rows = matching_rows("cue-a", 1000U, 0U, 24U);
  const local_acr::cli::AmbiguityGateResult result = local_acr::cli::check_ambiguity(
      database({trigger("cue-a")}, std::move(rows)));
  check(result.accepted, "single-trigger self matches are excluded");
  check(result.ambiguous_pairs.empty(), "self-only database has no ambiguous pairs");
}

void cross_trigger_runtime_passing_windows_reject() {
  std::vector<local_acr::FingerprintRow> rows = matching_rows("cue-a", 2000U, 0U, 24U);
  std::vector<local_acr::FingerprintRow> b = matching_rows("cue-b", 2000U, 8U, 24U);
  rows.insert(rows.end(), b.begin(), b.end());

  const local_acr::cli::AmbiguityGateResult result = local_acr::cli::check_ambiguity(
      database({trigger("cue-a"), trigger("cue-b")}, std::move(rows)));
  check(!result.accepted, "cross-trigger matching windows reject");
  check(!result.ambiguous_pairs.empty(), "ambiguous pair is reported");
  check(result.diagnostic_json.find("cue-a") != std::string::npos, "diagnostic names first trigger");
  check(result.diagnostic_json.find("cue-b") != std::string::npos, "diagnostic names second trigger");
}

void weak_hash_intersection_prefilter_accepts() {
  std::vector<local_acr::FingerprintRow> rows = matching_rows("cue-a", 3000U, 0U, 11U);
  std::vector<local_acr::FingerprintRow> b = matching_rows("cue-b", 3000U, 0U, 11U);
  rows.insert(rows.end(), b.begin(), b.end());

  const local_acr::cli::AmbiguityGateResult result = local_acr::cli::check_ambiguity(
      database({trigger("cue-a"), trigger("cue-b")}, std::move(rows)));
  check(result.accepted, "below runtime evidence threshold accepts");
}

void overlapping_windows_are_checked_on_255_ms_grid() {
  std::vector<local_acr::FingerprintRow> rows = matching_rows("cue-a", 4000U, 22U, 24U);
  std::vector<local_acr::FingerprintRow> b = matching_rows("cue-b", 4000U, 30U, 24U);
  rows.insert(rows.end(), b.begin(), b.end());

  const local_acr::cli::AmbiguityGateResult result = local_acr::cli::check_ambiguity(
      database({trigger("cue-a"), trigger("cue-b")}, std::move(rows)));
  check(!result.accepted, "non-zero overlapping window rejects");
  check(result.checked_windows > 1U, "gate checks overlapping grid windows");
}

}  // namespace

int main() {
  self_matches_are_excluded();
  cross_trigger_runtime_passing_windows_reject();
  weak_hash_intersection_prefilter_accepts();
  overlapping_windows_are_checked_on_255_ms_grid();
  return failures == 0 ? 0 : 1;
}
