#ifndef LOCAL_ACR_TOOLS_QUALIFICATION_RUNNER_HPP
#define LOCAL_ACR_TOOLS_QUALIFICATION_RUNNER_HPP

#include "support/status.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace local_acr::qualification {

enum class TrialSplit {
  Calibration,
  Holdout,
};

enum class TrialKind {
  Positive,
  Negative,
};

struct TrialTransform final {
  int snr_db = 0;
  int rt60_ms = 0;
  int gain_db = 0;
  int leading_offset_ms = 0;
  int start_phase_hop = 0;
};

struct CorpusTrial final {
  std::string id;
  TrialSplit split = TrialSplit::Calibration;
  TrialKind kind = TrialKind::Positive;
  std::string trigger_id;
  std::string source;
  std::uint32_t expected_callbacks = 0;
  TrialTransform transform;
};

struct CorpusManifest final {
  std::string corpus_id;
  std::uint32_t random_seed = 0;
  std::string database_path;
  std::vector<CorpusTrial> trials;
};

struct ManifestLoadResult final {
  Status status = Status::invalid_argument();
  CorpusManifest manifest;
};

struct TrialCounts final {
  std::uint32_t total = 0;
  std::uint32_t positive = 0;
  std::uint32_t negative = 0;
  std::uint32_t calibration = 0;
  std::uint32_t holdout = 0;
};

struct FixtureTrialResult final {
  std::string trial_id;
  TrialKind kind = TrialKind::Positive;
  bool recognized = false;
  std::uint32_t latency_ms = 0;
  std::uint32_t matched_position_error_ms = 0;
};

struct FixtureSummary final {
  std::uint32_t total = 0;
  std::uint32_t recognized_positive = 0;
  std::uint32_t missed_positive = 0;
  std::uint32_t false_callbacks = 0;
  std::uint32_t median_latency_ms = 0;
  std::uint32_t p95_latency_ms = 0;
  std::uint32_t max_matched_position_error_ms = 0;
};

struct FixtureReport final {
  std::vector<FixtureTrialResult> trials;
  FixtureSummary summary;
};

[[nodiscard]] ManifestLoadResult load_corpus_manifest(const std::filesystem::path& path);
[[nodiscard]] TrialCounts count_trials(const CorpusManifest& manifest) noexcept;
[[nodiscard]] std::string render_dry_run(const CorpusManifest& manifest);
[[nodiscard]] FixtureReport run_fixture_trials(const CorpusManifest& manifest);
[[nodiscard]] std::string render_jsonl(const FixtureReport& report);

}  // namespace local_acr::qualification

#endif
