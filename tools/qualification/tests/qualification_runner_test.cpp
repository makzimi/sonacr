#include "qualification_runner.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

#include <unistd.h>

namespace {

int failures = 0;

void check(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

std::filesystem::path temp_dir() {
  return std::filesystem::temp_directory_path() / "local-acr" /
         ("qualification-test-" + std::to_string(static_cast<long long>(::getpid())));
}

std::filesystem::path write_manifest(std::string_view text) {
  std::filesystem::create_directories(temp_dir());
  const std::filesystem::path path = temp_dir() / "corpus.json";
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << text;
  return path;
}

std::filesystem::path valid_manifest() {
  return write_manifest(R"JSON({
  "schemaVersion": 1,
  "corpusId": "venue-demo-mvp",
  "randomSeed": 424242,
  "databasePath": "androidApp/src/main/assets/venue-demo.lacrdb",
  "trials": [
    {
      "id": "calibration-positive-clean",
      "split": "calibration",
      "kind": "positive",
      "triggerId": "welcome-offer",
      "source": "fixtures/generated/welcome-offer.wav",
      "expectedCallbacks": 1,
      "transform": {
        "snrDb": 25,
        "rt60Ms": 200,
        "gainDb": 0,
        "leadingOffsetMs": 0,
        "startPhaseHop": 0
      }
    },
    {
      "id": "holdout-positive-noisy",
      "split": "holdout",
      "kind": "positive",
      "triggerId": "welcome-offer",
      "source": "fixtures/generated/welcome-offer.wav",
      "expectedCallbacks": 1,
      "transform": {
        "snrDb": 15,
        "rt60Ms": 600,
        "gainDb": -3,
        "leadingOffsetMs": 1000,
        "startPhaseHop": 17
      }
    },
    {
      "id": "holdout-negative-speech",
      "split": "holdout",
      "kind": "negative",
      "source": "fixtures/generated/negative-speech.wav",
      "expectedCallbacks": 0,
      "transform": {
        "snrDb": 0,
        "rt60Ms": 400,
        "gainDb": -6,
        "leadingOffsetMs": 500,
        "startPhaseHop": 4
      }
    }
  ]
})JSON");
}

void parses_manifest_and_counts_trials() {
  const auto result = local_acr::qualification::load_corpus_manifest(valid_manifest());
  check(result.status.ok(), "valid corpus manifest parses");
  check(result.manifest.corpus_id == "venue-demo-mvp", "corpus id parsed");
  check(result.manifest.random_seed == 424242U, "seed parsed");
  check(result.manifest.trials.size() == 3U, "trial count parsed");

  const auto counts = local_acr::qualification::count_trials(result.manifest);
  check(counts.total == 3U, "total count");
  check(counts.positive == 2U, "positive count");
  check(counts.negative == 1U, "negative count");
  check(counts.calibration == 1U, "calibration count");
  check(counts.holdout == 2U, "holdout count");
}

void rejects_invalid_holdout_split_and_transform_bounds() {
  const auto missing_holdout = local_acr::qualification::load_corpus_manifest(write_manifest(R"JSON({
    "schemaVersion": 1,
    "corpusId": "bad",
    "randomSeed": 7,
    "databasePath": "db.lacrdb",
    "trials": [
      {"id":"only-calibration","split":"calibration","kind":"positive","triggerId":"cue","source":"cue.wav","expectedCallbacks":1,
       "transform":{"snrDb":20,"rt60Ms":200,"gainDb":0,"leadingOffsetMs":0,"startPhaseHop":0}}
    ]
  })JSON"));
  check(!missing_holdout.status.ok(), "manifest without holdout rejects");

  const auto out_of_bounds = local_acr::qualification::load_corpus_manifest(write_manifest(R"JSON({
    "schemaVersion": 1,
    "corpusId": "bad",
    "randomSeed": 7,
    "databasePath": "db.lacrdb",
    "trials": [
      {"id":"cal","split":"calibration","kind":"positive","triggerId":"cue","source":"cue.wav","expectedCallbacks":1,
       "transform":{"snrDb":20,"rt60Ms":200,"gainDb":0,"leadingOffsetMs":0,"startPhaseHop":0}},
      {"id":"bad-holdout","split":"holdout","kind":"positive","triggerId":"cue","source":"cue.wav","expectedCallbacks":1,
       "transform":{"snrDb":9,"rt60Ms":900,"gainDb":0,"leadingOffsetMs":3000,"startPhaseHop":128}}
    ]
  })JSON"));
  check(!out_of_bounds.status.ok(), "out-of-bounds transform rejects");
}

void dry_run_and_fixture_run_are_deterministic() {
  const auto result = local_acr::qualification::load_corpus_manifest(valid_manifest());
  const std::string dry_run = local_acr::qualification::render_dry_run(result.manifest);
  check(dry_run.find("\"totalTrials\":3") != std::string::npos, "dry run includes total");
  check(dry_run.find("\"positiveTrials\":2") != std::string::npos, "dry run includes positive");
  check(dry_run.find("\"negativeTrials\":1") != std::string::npos, "dry run includes negative");

  const auto report = local_acr::qualification::run_fixture_trials(result.manifest);
  check(report.summary.total == 3U, "fixture total count");
  check(report.summary.recognized_positive == 2U, "fixture recognizes positives");
  check(report.summary.false_callbacks == 0U, "fixture has no false callbacks");
  check(report.summary.median_latency_ms == 2500, "fixture median latency");
  check(report.summary.p95_latency_ms == 3200, "fixture p95 latency");
  check(report.summary.max_matched_position_error_ms == 120, "fixture max position error");

  const std::string jsonl = local_acr::qualification::render_jsonl(report);
  check(jsonl.find("\"trialId\":\"holdout-negative-speech\"") != std::string::npos,
        "jsonl includes negative trial");
  check(jsonl.find("\"falseCallbacks\":0") != std::string::npos, "jsonl includes summary");
}

}  // namespace

int main() {
  parses_manifest_and_counts_trials();
  rejects_invalid_holdout_split_and_transform_bounds();
  dry_run_and_fixture_run_are_deterministic();
  return failures == 0 ? 0 : 1;
}
