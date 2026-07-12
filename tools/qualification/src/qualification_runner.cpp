#include "qualification_runner.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace local_acr::qualification {
namespace {

std::optional<std::string> read_file(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input.good()) {
    return std::nullopt;
  }
  std::ostringstream out;
  out << input.rdbuf();
  return out.str();
}

std::optional<std::string> string_field(const std::string& object, const std::string& key) {
  const std::regex pattern("\"" + key + "\"\\s*:\\s*\"([^\"]*)\"");
  std::smatch match;
  if (!std::regex_search(object, match, pattern)) {
    return std::nullopt;
  }
  return match[1].str();
}

std::optional<int> int_field(const std::string& object, const std::string& key) {
  const std::regex pattern("\"" + key + "\"\\s*:\\s*(-?[0-9]+)");
  std::smatch match;
  if (!std::regex_search(object, match, pattern)) {
    return std::nullopt;
  }
  return std::stoi(match[1].str());
}

std::optional<std::string> object_field(const std::string& object, const std::string& key) {
  const std::string needle = "\"" + key + "\"";
  const std::size_t key_pos = object.find(needle);
  if (key_pos == std::string::npos) {
    return std::nullopt;
  }
  const std::size_t open = object.find('{', key_pos + needle.size());
  if (open == std::string::npos) {
    return std::nullopt;
  }
  std::uint32_t depth = 0;
  for (std::size_t index = open; index < object.size(); ++index) {
    if (object[index] == '{') {
      ++depth;
    } else if (object[index] == '}') {
      --depth;
      if (depth == 0U) {
        return object.substr(open, index - open + 1U);
      }
    }
  }
  return std::nullopt;
}

std::optional<std::string> array_field(const std::string& object, const std::string& key) {
  const std::string needle = "\"" + key + "\"";
  const std::size_t key_pos = object.find(needle);
  if (key_pos == std::string::npos) {
    return std::nullopt;
  }
  const std::size_t open = object.find('[', key_pos + needle.size());
  if (open == std::string::npos) {
    return std::nullopt;
  }
  std::uint32_t depth = 0;
  for (std::size_t index = open; index < object.size(); ++index) {
    if (object[index] == '[') {
      ++depth;
    } else if (object[index] == ']') {
      --depth;
      if (depth == 0U) {
        return object.substr(open + 1U, index - open - 1U);
      }
    }
  }
  return std::nullopt;
}

std::vector<std::string> split_top_level_objects(const std::string& array_body) {
  std::vector<std::string> objects;
  std::uint32_t depth = 0;
  std::size_t start = std::string::npos;
  for (std::size_t index = 0; index < array_body.size(); ++index) {
    if (array_body[index] == '{') {
      if (depth == 0U) {
        start = index;
      }
      ++depth;
    } else if (array_body[index] == '}') {
      --depth;
      if (depth == 0U && start != std::string::npos) {
        objects.push_back(array_body.substr(start, index - start + 1U));
        start = std::string::npos;
      }
    }
  }
  return objects;
}

std::optional<TrialSplit> parse_split(const std::string& value) {
  if (value == "calibration") {
    return TrialSplit::Calibration;
  }
  if (value == "holdout") {
    return TrialSplit::Holdout;
  }
  return std::nullopt;
}

std::optional<TrialKind> parse_kind(const std::string& value) {
  if (value == "positive") {
    return TrialKind::Positive;
  }
  if (value == "negative") {
    return TrialKind::Negative;
  }
  return std::nullopt;
}

bool transform_in_bounds(const CorpusTrial& trial) noexcept {
  if (trial.kind == TrialKind::Positive &&
      (trial.transform.snr_db < 10 || trial.transform.snr_db > 25)) {
    return false;
  }
  if (trial.transform.rt60_ms < 200 || trial.transform.rt60_ms > 800) {
    return false;
  }
  if (trial.transform.leading_offset_ms < 0 || trial.transform.leading_offset_ms > 2000) {
    return false;
  }
  return trial.transform.start_phase_hop >= 0 && trial.transform.start_phase_hop < 128;
}

std::optional<CorpusTrial> parse_trial(const std::string& object) {
  const auto id = string_field(object, "id");
  const auto split_text = string_field(object, "split");
  const auto kind_text = string_field(object, "kind");
  const auto source = string_field(object, "source");
  const auto expected = int_field(object, "expectedCallbacks");
  const auto transform_object = object_field(object, "transform");
  if (!id.has_value() || !split_text.has_value() || !kind_text.has_value() || !source.has_value() ||
      !expected.has_value() || !transform_object.has_value()) {
    return std::nullopt;
  }
  const auto split = parse_split(*split_text);
  const auto kind = parse_kind(*kind_text);
  if (!split.has_value() || !kind.has_value()) {
    return std::nullopt;
  }

  CorpusTrial trial;
  trial.id = *id;
  trial.split = *split;
  trial.kind = *kind;
  trial.source = *source;
  trial.expected_callbacks = static_cast<std::uint32_t>(*expected);
  if (trial.kind == TrialKind::Positive) {
    const auto trigger_id = string_field(object, "triggerId");
    if (!trigger_id.has_value() || trigger_id->empty()) {
      return std::nullopt;
    }
    trial.trigger_id = *trigger_id;
  } else if (string_field(object, "triggerId").has_value()) {
    return std::nullopt;
  }

  const auto snr = int_field(*transform_object, "snrDb");
  const auto rt60 = int_field(*transform_object, "rt60Ms");
  const auto gain = int_field(*transform_object, "gainDb");
  const auto leading = int_field(*transform_object, "leadingOffsetMs");
  const auto phase = int_field(*transform_object, "startPhaseHop");
  if (!snr.has_value() || !rt60.has_value() || !gain.has_value() || !leading.has_value() ||
      !phase.has_value()) {
    return std::nullopt;
  }
  trial.transform = TrialTransform{
      .snr_db = *snr,
      .rt60_ms = *rt60,
      .gain_db = *gain,
      .leading_offset_ms = *leading,
      .start_phase_hop = *phase,
  };
  if (!transform_in_bounds(trial)) {
    return std::nullopt;
  }
  if ((trial.kind == TrialKind::Positive && trial.expected_callbacks != 1U) ||
      (trial.kind == TrialKind::Negative && trial.expected_callbacks != 0U)) {
    return std::nullopt;
  }
  return trial;
}

std::uint32_t percentile95(std::vector<std::uint32_t> values) {
  if (values.empty()) {
    return 0;
  }
  std::sort(values.begin(), values.end());
  const std::size_t index = ((values.size() - 1U) * 95U + 99U) / 100U;
  return values[std::min(index, values.size() - 1U)];
}

std::uint32_t median(std::vector<std::uint32_t> values) {
  if (values.empty()) {
    return 0;
  }
  std::sort(values.begin(), values.end());
  return values[(values.size() - 1U) / 2U];
}

const char* kind_string(const TrialKind kind) noexcept {
  return kind == TrialKind::Positive ? "positive" : "negative";
}

}  // namespace

ManifestLoadResult load_corpus_manifest(const std::filesystem::path& path) {
  ManifestLoadResult result;
  const std::optional<std::string> text = read_file(path);
  if (!text.has_value()) {
    return result;
  }
  const auto schema = int_field(*text, "schemaVersion");
  const auto corpus_id = string_field(*text, "corpusId");
  const auto seed = int_field(*text, "randomSeed");
  const auto database_path = string_field(*text, "databasePath");
  const auto trials_array = array_field(*text, "trials");
  if (!schema.has_value() || *schema != 1 || !corpus_id.has_value() || corpus_id->empty() ||
      !seed.has_value() || *seed < 0 || !database_path.has_value() || database_path->empty() ||
      !trials_array.has_value()) {
    return result;
  }

  CorpusManifest manifest{
      .corpus_id = *corpus_id,
      .random_seed = static_cast<std::uint32_t>(*seed),
      .database_path = *database_path,
  };
  for (const std::string& object : split_top_level_objects(*trials_array)) {
    std::optional<CorpusTrial> trial = parse_trial(object);
    if (!trial.has_value()) {
      return result;
    }
    const auto duplicate = std::find_if(manifest.trials.begin(), manifest.trials.end(),
                                        [&](const CorpusTrial& existing) {
                                          return existing.id == trial->id;
                                        });
    if (duplicate != manifest.trials.end()) {
      return result;
    }
    manifest.trials.push_back(std::move(*trial));
  }

  const TrialCounts counts = count_trials(manifest);
  if (counts.calibration == 0U || counts.holdout == 0U || counts.positive == 0U ||
      counts.negative == 0U) {
    return result;
  }
  result.status = Status::ok_status();
  result.manifest = std::move(manifest);
  return result;
}

TrialCounts count_trials(const CorpusManifest& manifest) noexcept {
  TrialCounts counts;
  counts.total = static_cast<std::uint32_t>(manifest.trials.size());
  for (const CorpusTrial& trial : manifest.trials) {
    if (trial.kind == TrialKind::Positive) {
      ++counts.positive;
    } else {
      ++counts.negative;
    }
    if (trial.split == TrialSplit::Calibration) {
      ++counts.calibration;
    } else {
      ++counts.holdout;
    }
  }
  return counts;
}

std::string render_dry_run(const CorpusManifest& manifest) {
  const TrialCounts counts = count_trials(manifest);
  std::ostringstream out;
  out << "{\"corpusId\":\"" << manifest.corpus_id << "\",\"seed\":" << manifest.random_seed
      << ",\"totalTrials\":" << counts.total << ",\"positiveTrials\":" << counts.positive
      << ",\"negativeTrials\":" << counts.negative
      << ",\"calibrationTrials\":" << counts.calibration << ",\"holdoutTrials\":"
      << counts.holdout << "}\n";
  return out.str();
}

FixtureReport run_fixture_trials(const CorpusManifest& manifest) {
  FixtureReport report;
  for (const CorpusTrial& trial : manifest.trials) {
    FixtureTrialResult result;
    result.trial_id = trial.id;
    result.kind = trial.kind;
    result.recognized = trial.kind == TrialKind::Positive;
    result.latency_ms = trial.kind == TrialKind::Positive
                            ? static_cast<std::uint32_t>(2500 + (trial.transform.start_phase_hop % 2) * 700)
                            : 0U;
    result.matched_position_error_ms =
        trial.kind == TrialKind::Positive
            ? static_cast<std::uint32_t>(80 + (trial.transform.leading_offset_ms / 25))
            : 0U;
    report.trials.push_back(result);
  }

  std::vector<std::uint32_t> latencies;
  for (const FixtureTrialResult& result : report.trials) {
    ++report.summary.total;
    if (result.kind == TrialKind::Positive) {
      if (result.recognized) {
        ++report.summary.recognized_positive;
        latencies.push_back(result.latency_ms);
      } else {
        ++report.summary.missed_positive;
      }
      report.summary.max_matched_position_error_ms =
          std::max(report.summary.max_matched_position_error_ms, result.matched_position_error_ms);
    } else if (result.recognized) {
      ++report.summary.false_callbacks;
    }
  }
  report.summary.median_latency_ms = median(latencies);
  report.summary.p95_latency_ms = percentile95(latencies);
  return report;
}

std::string render_jsonl(const FixtureReport& report) {
  std::ostringstream out;
  for (const FixtureTrialResult& result : report.trials) {
    out << "{\"trialId\":\"" << result.trial_id << "\",\"kind\":\"" << kind_string(result.kind)
        << "\",\"recognized\":" << (result.recognized ? "true" : "false")
        << ",\"latencyMs\":" << result.latency_ms << ",\"matchedPositionErrorMs\":"
        << result.matched_position_error_ms << "}\n";
  }
  out << "{\"summary\":{\"total\":" << report.summary.total
      << ",\"recognizedPositive\":" << report.summary.recognized_positive
      << ",\"missedPositive\":" << report.summary.missed_positive
      << ",\"falseCallbacks\":" << report.summary.false_callbacks
      << ",\"medianLatencyMs\":" << report.summary.median_latency_ms
      << ",\"p95LatencyMs\":" << report.summary.p95_latency_ms
      << ",\"maxMatchedPositionErrorMs\":" << report.summary.max_matched_position_error_ms
      << "}}\n";
  return out.str();
}

}  // namespace local_acr::qualification
