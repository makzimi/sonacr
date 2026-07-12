#include "ambiguity_gate.hpp"

#include "matcher/conservative_matcher.hpp"
#include "matcher/offset_accumulator.hpp"
#include "matcher/recognition_result.hpp"

#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace local_acr::cli {

namespace {

constexpr std::uint32_t kWindowFrames = 173;
constexpr std::uint32_t kWindowStepFrames = 22;
constexpr std::uint32_t kMinimumSharedHashes = 12;

struct TriggerRows final {
  std::string trigger_id;
  std::uint32_t duration_frames = 0;
  std::vector<Landmark> landmarks;
};

std::uint32_t duration_frames(const DatabaseTrigger& trigger) {
  const std::uint64_t frames = (trigger.duration_ms * 11025ULL) / (1000ULL * 128ULL);
  return static_cast<std::uint32_t>(std::max<std::uint64_t>(frames, 1ULL));
}

std::vector<TriggerRows> group_by_trigger(const SemanticDatabase& database) {
  std::vector<TriggerRows> grouped;
  grouped.reserve(database.triggers.size());
  for (const DatabaseTrigger& trigger : database.triggers) {
    grouped.push_back(TriggerRows{
        .trigger_id = trigger.trigger_id,
        .duration_frames = duration_frames(trigger),
        .landmarks = {},
    });
  }
  for (const FingerprintRow& row : database.fingerprints) {
    auto found = std::find_if(grouped.begin(), grouped.end(), [&](const TriggerRows& trigger) {
      return trigger.trigger_id == row.trigger_id;
    });
    if (found != grouped.end()) {
      found->landmarks.push_back(row.landmark);
    }
  }
  for (TriggerRows& trigger : grouped) {
    std::sort(trigger.landmarks.begin(), trigger.landmarks.end(), [](const Landmark& lhs, const Landmark& rhs) {
      if (lhs.anchor_time_frame != rhs.anchor_time_frame) {
        return lhs.anchor_time_frame < rhs.anchor_time_frame;
      }
      return lhs.hash < rhs.hash;
    });
  }
  return grouped;
}

std::uint32_t max_frame(const TriggerRows& trigger) {
  std::uint32_t frame = 0;
  for (const Landmark& landmark : trigger.landmarks) {
    frame = std::max(frame, landmark.anchor_time_frame);
  }
  return frame;
}

std::vector<Landmark> window_landmarks(const TriggerRows& trigger, std::uint32_t start_frame) {
  std::vector<Landmark> out;
  const std::uint32_t end_frame = start_frame + kWindowFrames;
  for (const Landmark& landmark : trigger.landmarks) {
    if (landmark.anchor_time_frame >= start_frame && landmark.anchor_time_frame < end_frame) {
      out.push_back(landmark);
    }
  }
  return out;
}

std::uint32_t shared_hash_count(const std::vector<Landmark>& query, const TriggerRows& target) {
  std::set<std::uint32_t> query_hashes;
  for (const Landmark& landmark : query) {
    query_hashes.insert(landmark.hash);
  }
  std::set<std::uint32_t> shared;
  for (const Landmark& landmark : target.landmarks) {
    if (query_hashes.contains(landmark.hash)) {
      shared.insert(landmark.hash);
    }
  }
  return static_cast<std::uint32_t>(shared.size());
}

std::vector<AlignedCandidate> candidates_for(const std::vector<Landmark>& query, const TriggerRows& target) {
  std::map<std::int64_t, std::set<std::uint32_t>> aligned_query_ids_by_bucket;
  for (std::uint32_t query_index = 0; query_index < query.size(); ++query_index) {
    const Landmark& query_landmark = query[query_index];
    for (const Landmark& target_landmark : target.landmarks) {
      if (target_landmark.hash != query_landmark.hash) {
        continue;
      }
      const std::int64_t delta = static_cast<std::int64_t>(target_landmark.anchor_time_frame) -
                                 static_cast<std::int64_t>(query_landmark.anchor_time_frame);
      const std::int64_t bucket = delta >= 0 ? delta / 2 : -(((-delta) + 1) / 2);
      aligned_query_ids_by_bucket[bucket].insert(query_index);
    }
  }

  std::vector<AlignedCandidate> candidates;
  candidates.reserve(aligned_query_ids_by_bucket.size());
  for (const auto& [bucket, ids] : aligned_query_ids_by_bucket) {
    const auto count = static_cast<std::uint32_t>(ids.size());
    candidates.push_back(AlignedCandidate{
        .trigger_id = target.trigger_id,
        .center_bucket = bucket,
        .aligned_query_ids = std::vector<std::uint32_t>(ids.begin(), ids.end()),
        .aligned_count = count,
        .aligned_ratio = query.empty() ? 0.0 : static_cast<double>(count) / static_cast<double>(query.size()),
    });
  }
  return candidates;
}

std::vector<TriggerCueDuration> durations_for(const std::vector<TriggerRows>& triggers) {
  std::vector<TriggerCueDuration> durations;
  durations.reserve(triggers.size());
  for (const TriggerRows& trigger : triggers) {
    durations.push_back(TriggerCueDuration{
        .trigger_id = trigger.trigger_id,
        .duration_frames = trigger.duration_frames,
    });
  }
  return durations;
}

bool runtime_gate_recognizes(const std::vector<Landmark>& query,
                             const TriggerRows& target,
                             const std::vector<TriggerCueDuration>& durations,
                             std::uint32_t newest_query_time_frame) {
  std::vector<AlignedCandidate> candidates = candidates_for(query, target);
  if (candidates.empty()) {
    return false;
  }
  std::sort(candidates.begin(), candidates.end(), [](const AlignedCandidate& lhs, const AlignedCandidate& rhs) {
    if (lhs.aligned_count != rhs.aligned_count) {
      return lhs.aligned_count > rhs.aligned_count;
    }
    return lhs.center_bucket < rhs.center_bucket;
  });
  const PreviousWinner previous{
      .trigger_id = candidates.front().trigger_id,
      .center_bucket = candidates.front().center_bucket,
  };
  const MatcherEvaluation evaluation{
      .candidates = std::move(candidates),
      .previous_winner = previous,
      .unique_query_landmark_count = static_cast<std::uint32_t>(query.size()),
      .newest_query_time_frame = newest_query_time_frame,
      .newest_source_frame = newest_query_time_frame,
      .trigger_durations = durations,
  };
  return ConservativeMatcher{}.evaluate(evaluation).recognized.has_value();
}

std::string diagnostic_json(const std::vector<std::string>& pairs, std::uint32_t checked_windows) {
  std::string out = "{\"accepted\":false,\"checkedWindows\":" + std::to_string(checked_windows) +
                    ",\"ambiguousPairs\":[";
  for (std::size_t i = 0; i < pairs.size(); ++i) {
    if (i != 0U) {
      out.push_back(',');
    }
    out.push_back('"');
    out += pairs[i];
    out.push_back('"');
  }
  out += "]}";
  return out;
}

}  // namespace

AmbiguityGateResult check_ambiguity(const SemanticDatabase& database) {
  const std::vector<TriggerRows> triggers = group_by_trigger(database);
  const std::vector<TriggerCueDuration> durations = durations_for(triggers);
  AmbiguityGateResult result;

  std::set<std::string> pair_set;
  for (const TriggerRows& source : triggers) {
    const std::uint32_t last_start = max_frame(source);
    for (std::uint32_t start = 0; start <= last_start; start += kWindowStepFrames) {
      const std::vector<Landmark> query = window_landmarks(source, start);
      if (query.size() < kMinimumSharedHashes) {
        continue;
      }
      ++result.checked_windows;
      const std::uint32_t newest_query_time_frame = start + kWindowFrames - 1U;
      for (const TriggerRows& target : triggers) {
        if (target.trigger_id == source.trigger_id) {
          continue;
        }
        if (shared_hash_count(query, target) < kMinimumSharedHashes) {
          continue;
        }
        if (runtime_gate_recognizes(query, target, durations, newest_query_time_frame)) {
          const std::string pair = source.trigger_id < target.trigger_id
                                       ? source.trigger_id + "<->" + target.trigger_id
                                       : target.trigger_id + "<->" + source.trigger_id;
          pair_set.insert(pair);
        }
      }
    }
  }

  result.ambiguous_pairs.assign(pair_set.begin(), pair_set.end());
  result.accepted = result.ambiguous_pairs.empty();
  result.diagnostic_json = result.accepted
                               ? "{\"accepted\":true,\"checkedWindows\":" +
                                     std::to_string(result.checked_windows) + ",\"ambiguousPairs\":[]}"
                               : diagnostic_json(result.ambiguous_pairs, result.checked_windows);
  return result;
}

}  // namespace local_acr::cli
