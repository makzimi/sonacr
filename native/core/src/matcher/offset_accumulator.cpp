#include "matcher/offset_accumulator.hpp"

#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace local_acr {

namespace {

std::int64_t floor_divide_by_two(const std::int64_t value) noexcept {
  if (value >= 0) {
    return value / 2;
  }
  return -(((-value) + 1) / 2);
}

using BucketMap = std::map<std::int64_t, std::set<std::uint32_t>>;
using TriggerBuckets = std::map<std::string, BucketMap>;

std::set<std::uint32_t> union_neighborhood(const BucketMap& buckets,
                                           const std::int64_t center) {
  std::set<std::uint32_t> ids;
  for (std::int64_t bucket = center - 1; bucket <= center + 1; ++bucket) {
    const auto found = buckets.find(bucket);
    if (found != buckets.end()) {
      ids.insert(found->second.begin(), found->second.end());
    }
  }
  return ids;
}

AlignedCandidate make_candidate(std::string trigger_id,
                                std::int64_t center,
                                std::set<std::uint32_t> ids,
                                std::uint32_t total_query_count) {
  std::vector<std::uint32_t> query_ids(ids.begin(), ids.end());
  const auto aligned_count = static_cast<std::uint32_t>(query_ids.size());
  const double ratio = total_query_count == 0U
                           ? 0.0
                           : static_cast<double>(aligned_count) /
                                 static_cast<double>(total_query_count);
  return AlignedCandidate{
      .trigger_id = std::move(trigger_id),
      .center_bucket = center,
      .aligned_query_ids = std::move(query_ids),
      .aligned_count = aligned_count,
      .aligned_ratio = ratio,
  };
}

bool candidate_less(const AlignedCandidate& lhs, const AlignedCandidate& rhs) {
  if (lhs.aligned_count != rhs.aligned_count) {
    return lhs.aligned_count > rhs.aligned_count;
  }
  const std::int64_t lhs_abs = lhs.center_bucket < 0 ? -lhs.center_bucket : lhs.center_bucket;
  const std::int64_t rhs_abs = rhs.center_bucket < 0 ? -rhs.center_bucket : rhs.center_bucket;
  if (lhs_abs != rhs_abs) {
    return lhs_abs < rhs_abs;
  }
  if (lhs.center_bucket != rhs.center_bucket) {
    return lhs.center_bucket < rhs.center_bucket;
  }
  return lhs.trigger_id < rhs.trigger_id;
}

std::vector<AlignedCandidate> ranked_candidates_for_trigger(const std::string& trigger_id,
                                                            const BucketMap& buckets,
                                                            std::uint32_t total_query_count) {
  std::vector<AlignedCandidate> ranked;
  ranked.reserve(buckets.size());
  for (const auto& [center, ignored] : buckets) {
    (void)ignored;
    std::set<std::uint32_t> ids = union_neighborhood(buckets, center);
    if (!ids.empty()) {
      ranked.push_back(make_candidate(trigger_id, center, std::move(ids), total_query_count));
    }
  }
  std::sort(ranked.begin(), ranked.end(), candidate_less);
  if (ranked.empty()) {
    return ranked;
  }

  std::vector<AlignedCandidate> selected;
  selected.push_back(ranked.front());
  for (std::size_t index = 1; index < ranked.size(); ++index) {
    const std::int64_t delta = ranked[index].center_bucket - selected.front().center_bucket;
    const std::int64_t distance = delta < 0 ? -delta : delta;
    if (distance >= 3) {
      selected.push_back(ranked[index]);
      break;
    }
  }
  return selected;
}

}  // namespace

Status OffsetAccumulator::add_vote(const CandidateVote& vote) {
  if (vote.trigger_id.empty()) {
    return Status::invalid_argument();
  }
  votes_.push_back(vote);
  return Status::ok_status();
}

std::vector<AlignedCandidate> OffsetAccumulator::finish(const std::uint32_t total_query_count) const {
  TriggerBuckets trigger_buckets;
  for (const CandidateVote& vote : votes_) {
    trigger_buckets[vote.trigger_id][floor_divide_by_two(vote.offset_frames)].insert(vote.query_id);
  }

  std::vector<AlignedCandidate> candidates;
  for (const auto& [trigger_id, buckets] : trigger_buckets) {
    std::vector<AlignedCandidate> selected =
        ranked_candidates_for_trigger(trigger_id, buckets, total_query_count);
    candidates.insert(candidates.end(), selected.begin(), selected.end());
  }
  std::sort(candidates.begin(), candidates.end(), candidate_less);
  return candidates;
}

void OffsetAccumulator::reset() noexcept {
  votes_.clear();
}

}  // namespace local_acr
