#ifndef LOCAL_ACR_MATCHER_OFFSET_ACCUMULATOR_HPP
#define LOCAL_ACR_MATCHER_OFFSET_ACCUMULATOR_HPP

#include "support/status.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace local_acr {

struct CandidateVote final {
  std::string trigger_id;
  std::int64_t offset_frames = 0;
  std::uint32_t query_id = 0;
};

struct AlignedCandidate final {
  std::string trigger_id;
  std::int64_t center_bucket = 0;
  std::vector<std::uint32_t> aligned_query_ids;
  std::uint32_t aligned_count = 0;
  double aligned_ratio = 0.0;
};

class OffsetAccumulator final {
 public:
  [[nodiscard]] Status add_vote(const CandidateVote& vote);
  [[nodiscard]] std::vector<AlignedCandidate> finish(std::uint32_t total_query_count) const;
  void reset() noexcept;

 private:
  std::vector<CandidateVote> votes_;
};

}  // namespace local_acr

#endif
