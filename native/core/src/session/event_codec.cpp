#include "session/event_codec.hpp"

#include <cstring>

namespace local_acr {

namespace {

lacr_event_type_t event_type(const RecognizerEventType type) noexcept {
  return type == RecognizerEventType::Recognition ? LACR_EVENT_RECOGNITION : LACR_EVENT_SESSION_ERROR;
}

lacr_error_code_t error_code(const RecognizerError error) noexcept {
  switch (error) {
    case RecognizerError::None:
      return LACR_ERROR_NONE;
    case RecognizerError::QueryDensityExceeded:
      return LACR_ERROR_QUERY_DENSITY_EXCEEDED;
    case RecognizerError::AudioDiscontinuity:
      return LACR_ERROR_AUDIO_DISCONTINUITY;
    case RecognizerError::NativeEngineFailure:
      return LACR_ERROR_NATIVE_ENGINE_FAILURE;
  }
  return LACR_ERROR_NATIVE_ENGINE_FAILURE;
}

}  // namespace

EncodedEvent encode_event(const RecognizerEvent& source,
                          lacr_event_t& event,
                          std::uint8_t* payload,
                          const std::size_t payload_capacity) noexcept {
  const std::size_t required = source.recognition.trigger_id.size();
  if (required > payload_capacity) {
    return EncodedEvent{.status = LACR_STATUS_BUFFER_TOO_SMALL, .required = required};
  }
  if (required != 0U && payload != nullptr) {
    std::memcpy(payload, source.recognition.trigger_id.data(), required);
  }
  event = lacr_event_t{
      .abi_version = LACR_ABI_VERSION,
      .type = event_type(source.type),
      .error = error_code(source.error),
      .generation = source.generation,
      .newest_source_frame = source.newest_source_frame,
      .matched_cue_time_frame = source.recognition.matched_cue_time_frame,
      .aligned_count = source.recognition.aligned_count,
      .confidence = static_cast<float>(source.recognition.confidence),
      .aligned_ratio = static_cast<float>(source.recognition.aligned_ratio),
      .trigger_id_offset = 0,
      .trigger_id_length = static_cast<std::uint32_t>(required),
  };
  return EncodedEvent{.status = LACR_STATUS_OK, .required = required};
}

}  // namespace local_acr
