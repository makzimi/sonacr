#ifndef LOCAL_ACR_MATCHER_MATCHER_PROFILE_HPP
#define LOCAL_ACR_MATCHER_MATCHER_PROFILE_HPP

#include <cstdint>
#include <string_view>

namespace local_acr {

struct MatcherProfile final {
  std::string_view name;
  std::uint32_t minimum_aligned_landmarks = 12;
  double minimum_aligned_ratio = 0.12;
  std::uint32_t minimum_winner_margin = 5;
  double minimum_runner_up_ratio = 1.25;
  std::int64_t maximum_offset_bucket_delta = 1;
  double ambiguous_secondary_ratio = 0.80;
};

inline constexpr MatcherProfile kConservativeMatcherProfile{
    .name = "conservative-v1",
    .minimum_aligned_landmarks = 8,
    // Inactive at the 512-landmark query cap: 8 / 512 = 0.0156 > 0.015, so the evidence gate always binds first.
    .minimum_aligned_ratio = 0.015,
    .minimum_winner_margin = 5,
    .minimum_runner_up_ratio = 1.25,
    .maximum_offset_bucket_delta = 1,
    .ambiguous_secondary_ratio = 0.80,
};

}  // namespace local_acr

#endif
