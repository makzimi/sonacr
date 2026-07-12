#ifndef LOCAL_ACR_SESSION_EVENT_CODEC_HPP
#define LOCAL_ACR_SESSION_EVENT_CODEC_HPP

#include "local_acr/local_acr.h"
#include "session/event.hpp"

#include <cstddef>
#include <cstdint>

namespace local_acr {

struct EncodedEvent final {
  lacr_status_t status = LACR_STATUS_OK;
  std::size_t required = 0;
};

[[nodiscard]] EncodedEvent encode_event(const RecognizerEvent& source,
                                        lacr_event_t& event,
                                        std::uint8_t* payload,
                                        std::size_t payload_capacity) noexcept;

}  // namespace local_acr

#endif
