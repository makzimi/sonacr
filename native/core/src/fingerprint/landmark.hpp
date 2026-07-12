#ifndef LOCAL_ACR_FINGERPRINT_LANDMARK_HPP
#define LOCAL_ACR_FINGERPRINT_LANDMARK_HPP

#include "support/status.hpp"

#include <cstdint>
#include <span>

namespace local_acr {

struct Landmark final {
  std::uint32_t hash = 0;
  std::uint32_t anchor_time_frame = 0;

  [[nodiscard]] friend constexpr bool operator==(const Landmark& lhs,
                                                 const Landmark& rhs) noexcept = default;
};

class LandmarkSink {
 public:
  virtual ~LandmarkSink() = default;
  virtual Status consume(std::span<const Landmark> landmarks) noexcept = 0;
};

}  // namespace local_acr

#endif
