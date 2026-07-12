#ifndef LOCAL_ACR_MATCHER_QUERY_LANDMARK_HPP
#define LOCAL_ACR_MATCHER_QUERY_LANDMARK_HPP

#include <cstdint>

namespace local_acr {

struct QueryLandmark final {
  std::uint32_t query_id = 0;
  std::uint32_t hash = 0;
  std::uint32_t time_frame = 0;

  [[nodiscard]] friend constexpr bool operator==(const QueryLandmark& lhs,
                                                 const QueryLandmark& rhs) noexcept = default;
};

}  // namespace local_acr

#endif
