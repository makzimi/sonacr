#ifndef LOCAL_ACR_SESSION_EVENT_HPP
#define LOCAL_ACR_SESSION_EVENT_HPP

#include "matcher/recognition_result.hpp"

#include <cstdint>

namespace local_acr {

enum class RecognizerEventType : std::uint8_t {
  Recognition = 1,
  SessionError = 2,
};

enum class RecognizerError : std::uint8_t {
  None = 0,
  QueryDensityExceeded,
  AudioDiscontinuity,
  NativeEngineFailure,
};

struct RecognizerEvent final {
  RecognizerEventType type = RecognizerEventType::SessionError;
  std::uint64_t generation = 0;
  NativeRecognitionResult recognition;
  RecognizerError error = RecognizerError::None;
  std::uint64_t newest_source_frame = 0;
};

}  // namespace local_acr

#endif
