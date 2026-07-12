#ifndef LOCAL_ACR_MATCHER_CANDIDATE_LOOKUP_HPP
#define LOCAL_ACR_MATCHER_CANDIDATE_LOOKUP_HPP

#include "database/database_reader.hpp"
#include "matcher/offset_accumulator.hpp"
#include "matcher/query_landmark.hpp"
#include "support/status.hpp"

#include <span>
#include <vector>

namespace local_acr {

struct CandidateLookupResult final {
  Status status = Status::ok_status();
  std::vector<AlignedCandidate> candidates;
};

class CandidateLookup final {
 public:
  [[nodiscard]] CandidateLookupResult lookup(DatabaseReader& reader,
                                             std::span<const QueryLandmark> query) const noexcept;
};

}  // namespace local_acr

#endif
