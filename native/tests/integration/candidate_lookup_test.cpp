#include "database/database_reader.hpp"
#include "database/database_writer.hpp"
#include "matcher/candidate_lookup.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include <unistd.h>

namespace {

int failures = 0;

void check(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

std::filesystem::path temp_path(std::string_view name) {
  return std::filesystem::temp_directory_path() / "local-acr" /
         ("candidate-lookup-" + std::to_string(static_cast<long long>(::getpid()))) /
         std::string(name);
}

local_acr::DatabaseTrigger trigger(std::string_view id) {
  return local_acr::DatabaseTrigger{
      .trigger_id = std::string(id),
      .display_name = std::string(id),
      .duration_ms = 1000,
      .metadata_json = "{}",
  };
}

local_acr::FingerprintRow row(std::string_view trigger_id,
                              std::uint32_t hash,
                              std::uint32_t time_frame) {
  return local_acr::FingerprintRow{
      .trigger_id = std::string(trigger_id),
      .landmark = local_acr::Landmark{hash, time_frame},
  };
}

local_acr::SemanticDatabase database(std::vector<local_acr::DatabaseTrigger> triggers,
                                     std::vector<local_acr::FingerprintRow> fingerprints) {
  return local_acr::SemanticDatabase{
      .metadata =
          local_acr::DatabaseMetadata{
              .database_id = "candidate-lookup",
              .database_version = "1",
              .build_report_json = "{}",
              .decoder_version = "test",
          },
      .triggers = std::move(triggers),
      .fingerprints = std::move(fingerprints),
  };
}

local_acr::DatabaseReader open_reader(std::string_view name,
                                      const local_acr::SemanticDatabase& db) {
  std::filesystem::create_directories(temp_path("").parent_path());
  const std::filesystem::path path = temp_path(name);
  std::filesystem::remove(path);
  check(local_acr::DatabaseWriter::write(path, db).ok(), "fixture database writes");
  local_acr::DatabaseReader reader;
  check(reader.open(path).ok(), "fixture database opens");
  return reader;
}

void lookup_expands_repeated_hashes_without_vote_inflation() {
  local_acr::DatabaseReader reader = open_reader(
      "candidate-repeated-hash.lacrdb",
      database({trigger("venue-a")},
               {
                   row("venue-a", 0x001122U, 100),
                   row("venue-a", 0x001122U, 101),
                   row("venue-a", 0x001122U, 102),
               }));

  const std::vector<local_acr::QueryLandmark> query{
      local_acr::QueryLandmark{.query_id = 7, .hash = 0x001122U, .time_frame = 100},
      local_acr::QueryLandmark{.query_id = 8, .hash = 0x001122U, .time_frame = 101},
  };

  local_acr::CandidateLookup lookup;
  auto result = lookup.lookup(reader, query);
  check(result.status.ok(), "candidate lookup succeeds");
  check(result.candidates.size() == 1, "one aligned candidate");
  if (!result.candidates.empty()) {
    check(result.candidates[0].trigger_id == "venue-a", "candidate trigger matches");
    check(result.candidates[0].center_bucket == 0, "neighboring offsets align at center zero");
    check(result.candidates[0].aligned_count == 2, "repeated postings do not inflate query votes");
    check(result.candidates[0].aligned_ratio == 1.0, "aligned ratio counts unique query identities");
  }
}

void lookup_chunks_more_than_256_hashes_and_keeps_trigger_isolation() {
  std::vector<local_acr::FingerprintRow> fingerprints{
      row("venue-a", 0x000001U, 50),
      row("venue-b", 0x000001U, 70),
      row("venue-b", 0x00012DU, 120),
  };
  local_acr::DatabaseReader reader =
      open_reader("candidate-chunks.lacrdb", database({trigger("venue-a"), trigger("venue-b")},
                                                       std::move(fingerprints)));

  std::vector<local_acr::QueryLandmark> query;
  query.reserve(301);
  query.push_back(local_acr::QueryLandmark{.query_id = 1, .hash = 0x000001U, .time_frame = 40});
  for (std::uint32_t i = 2; i <= 300; ++i) {
    query.push_back(local_acr::QueryLandmark{.query_id = i, .hash = i, .time_frame = i});
  }
  query.push_back(local_acr::QueryLandmark{.query_id = 301, .hash = 0x00012DU, .time_frame = 100});

  local_acr::CandidateLookup lookup;
  auto result = lookup.lookup(reader, query);
  check(result.status.ok(), "chunked lookup succeeds");
  check(result.candidates.size() >= 2, "candidates are produced from multiple chunks/triggers");
  check(std::any_of(result.candidates.begin(), result.candidates.end(), [](const auto& candidate) {
          return candidate.trigger_id == "venue-a" && candidate.center_bucket == 5;
        }),
        "venue-a offset is isolated");
  check(std::any_of(result.candidates.begin(), result.candidates.end(), [](const auto& candidate) {
          return candidate.trigger_id == "venue-b" && candidate.center_bucket == 10;
        }),
        "venue-b second chunk offset is found");
}

void lookup_stops_when_expansion_cutoff_is_reached() {
  std::vector<local_acr::FingerprintRow> fingerprints;
  fingerprints.reserve(128);
  for (std::uint32_t i = 0; i < 128; ++i) {
    fingerprints.push_back(row("venue-a", 0x00F00DU, i));
  }
  local_acr::DatabaseReader reader =
      open_reader("candidate-expansion-cutoff.lacrdb", database({trigger("venue-a")},
                                                                 std::move(fingerprints)));

  std::vector<local_acr::QueryLandmark> query;
  query.reserve(512);
  for (std::uint32_t i = 0; i < 512; ++i) {
    query.push_back(local_acr::QueryLandmark{.query_id = i, .hash = 0x00F00DU, .time_frame = i});
  }

  local_acr::CandidateLookup lookup;
  auto result = lookup.lookup(reader, query);
  check(result.status.code() == local_acr::StatusCode::ResourceLimitExceeded,
        "lookup refuses at the 65536-expansion cutoff");
  check(result.candidates.empty(), "cutoff returns no partial candidates");
}

}  // namespace

int main() {
  lookup_expands_repeated_hashes_without_vote_inflation();
  lookup_chunks_more_than_256_hashes_and_keeps_trigger_isolation();
  lookup_stops_when_expansion_cutoff_is_reached();
  return failures == 0 ? 0 : 1;
}
