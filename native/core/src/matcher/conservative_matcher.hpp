#ifndef LOCAL_ACR_MATCHER_CONSERVATIVE_MATCHER_HPP
#define LOCAL_ACR_MATCHER_CONSERVATIVE_MATCHER_HPP

#include "matcher/matcher_profile.hpp"
#include "matcher/recognition_result.hpp"

namespace local_acr {

class ConservativeMatcher final {
 public:
  [[nodiscard]] MatcherResult evaluate(const MatcherEvaluation& evaluation) const noexcept;
  [[nodiscard]] constexpr const MatcherProfile& profile() const noexcept {
    return kConservativeMatcherProfile;
  }
};

}  // namespace local_acr

#endif
