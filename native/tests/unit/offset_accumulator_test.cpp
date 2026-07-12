#include "matcher/offset_accumulator.hpp"

#include <cstdint>
#include <iostream>
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

local_acr::CandidateVote vote(std::string_view trigger_id,
                              std::int64_t offset,
                              std::uint32_t query_id) {
  return local_acr::CandidateVote{
      .trigger_id = std::string(trigger_id),
      .offset_frames = offset,
      .query_id = query_id,
  };
}

void floor_quantizes_negative_offsets_by_two_frames() {
  local_acr::OffsetAccumulator accumulator;
  check(accumulator.add_vote(vote("a", -1, 1)).ok(), "add -1 frame vote");
  check(accumulator.add_vote(vote("a", -2, 2)).ok(), "add -2 frame vote");

  const std::vector<local_acr::AlignedCandidate> candidates = accumulator.finish(4);
  check(candidates.size() == 1, "one candidate from negative offsets");
  if (!candidates.empty()) {
    check(candidates[0].center_bucket == -1, "-1 and -2 frames floor into bucket -1");
    check(candidates[0].aligned_count == 2, "neighbor union counts both unique query ids");
    check(candidates[0].aligned_query_ids.size() == 2, "aligned ids are unique");
    check(candidates[0].aligned_ratio == 0.5, "aligned ratio uses total query count");
  }
}

void repeated_hash_expansions_cannot_inflate_unique_votes() {
  local_acr::OffsetAccumulator accumulator;
  check(accumulator.add_vote(vote("a", 0, 7)).ok(), "add first repeated query vote");
  check(accumulator.add_vote(vote("a", 1, 7)).ok(), "add neighboring repeated query vote");
  check(accumulator.add_vote(vote("a", 2, 7)).ok(), "add second neighboring repeated query vote");
  check(accumulator.add_vote(vote("a", 2, 8)).ok(), "add distinct query vote");

  const std::vector<local_acr::AlignedCandidate> candidates = accumulator.finish(4);
  check(candidates.size() == 1, "one repeated-hash candidate");
  if (!candidates.empty()) {
    check(candidates[0].aligned_count == 2, "unique query ids, not postings, define count");
    check(candidates[0].aligned_query_ids.size() == 2, "deduplicated query id set is exposed");
  }
}

void trigger_isolation_and_deterministic_ties() {
  local_acr::OffsetAccumulator accumulator;
  check(accumulator.add_vote(vote("b", 0, 1)).ok(), "add b tie vote");
  check(accumulator.add_vote(vote("a", 0, 2)).ok(), "add a tie vote");
  check(accumulator.add_vote(vote("b", 20, 3)).ok(), "add b secondary vote");
  check(accumulator.add_vote(vote("a", 20, 4)).ok(), "add a secondary vote");

  const std::vector<local_acr::AlignedCandidate> candidates = accumulator.finish(4);
  check(candidates.size() == 4, "each trigger/center is independent");
  if (candidates.size() >= 4) {
    check(candidates[0].trigger_id == "a", "trigger id breaks equal-score ties");
    check(candidates[0].center_bucket == 0, "absolute offset breaks centers before signed offset");
    check(candidates[1].trigger_id == "b", "same center for next trigger follows");
    check(candidates[2].center_bucket == 10, "secondary centers at least three buckets away are kept");
  }
}

void secondary_centers_must_be_disjoint_from_primary_neighborhood() {
  local_acr::OffsetAccumulator accumulator;
  check(accumulator.add_vote(vote("a", 0, 1)).ok(), "add primary center vote");
  check(accumulator.add_vote(vote("a", 2, 2)).ok(), "add adjacent center vote");
  check(accumulator.add_vote(vote("a", 6, 3)).ok(), "add disjoint secondary vote");

  const std::vector<local_acr::AlignedCandidate> candidates = accumulator.finish(3);
  check(candidates.size() == 2, "only disjoint secondary survives");
  if (candidates.size() == 2) {
    check(candidates[0].center_bucket == 0, "primary center selected");
    check(candidates[1].center_bucket == 3, "secondary is separated by at least three buckets");
  }
}

}  // namespace

int main() {
  floor_quantizes_negative_offsets_by_two_frames();
  repeated_hash_expansions_cannot_inflate_unique_votes();
  trigger_isolation_and_deterministic_ties();
  secondary_centers_must_be_disjoint_from_primary_neighborhood();
  return failures == 0 ? 0 : 1;
}
