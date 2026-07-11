#ifndef LOCAL_ACR_SUPPORT_STATUS_HPP
#define LOCAL_ACR_SUPPORT_STATUS_HPP

#include <cstdint>

namespace local_acr {

enum class StatusCode : std::uint8_t {
  Ok = 0,
  InvalidArgument,
  ResourceLimitExceeded,
  AudioDiscontinuity,
};

class Status final {
 public:
  [[nodiscard]] static constexpr Status ok_status() noexcept {
    return Status(StatusCode::Ok);
  }

  [[nodiscard]] static constexpr Status invalid_argument() noexcept {
    return Status(StatusCode::InvalidArgument);
  }

  [[nodiscard]] static constexpr Status resource_limit_exceeded() noexcept {
    return Status(StatusCode::ResourceLimitExceeded);
  }

  [[nodiscard]] static constexpr Status audio_discontinuity() noexcept {
    return Status(StatusCode::AudioDiscontinuity);
  }

  [[nodiscard]] constexpr bool ok() const noexcept {
    return code_ == StatusCode::Ok;
  }

  [[nodiscard]] constexpr StatusCode code() const noexcept {
    return code_;
  }

 private:
  explicit constexpr Status(StatusCode code) noexcept : code_(code) {}

  StatusCode code_;
};

}  // namespace local_acr

#endif
