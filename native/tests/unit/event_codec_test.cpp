#include "session/event_codec.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>

namespace {

int failures = 0;

void check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

void buffer_too_small_reports_required_bytes() {
  local_acr::RecognizerEvent source{
      .type = local_acr::RecognizerEventType::Recognition,
      .generation = 7,
      .recognition =
          local_acr::NativeRecognitionResult{
              .trigger_id = "trigger",
              .confidence = 0.5,
              .matched_cue_time_frame = 42,
              .newest_source_frame = 123,
              .aligned_count = 12,
              .aligned_ratio = 0.12,
          },
      .error = local_acr::RecognizerError::None,
      .newest_source_frame = 123,
  };
  lacr_event_t event{};
  std::array<std::uint8_t, 3> payload{};
  const local_acr::EncodedEvent too_small =
      local_acr::encode_event(source, event, payload.data(), payload.size());
  check(too_small.status == LACR_STATUS_BUFFER_TOO_SMALL, "small buffer returns BUFFER_TOO_SMALL");
  check(too_small.required == 7U, "required size is trigger id length");

  std::array<std::uint8_t, 7> exact{};
  const local_acr::EncodedEvent exact_result =
      local_acr::encode_event(source, event, exact.data(), exact.size());
  check(exact_result.status == LACR_STATUS_OK, "exact buffer succeeds");
  check(event.trigger_id_length == 7U, "event records trigger id length");
  check(exact[0] == static_cast<std::uint8_t>('t') && exact[6] == static_cast<std::uint8_t>('r'),
        "payload contains trigger id bytes");
}

}  // namespace

int main() {
  buffer_too_small_reports_required_bytes();
  return failures == 0 ? 0 : 1;
}
