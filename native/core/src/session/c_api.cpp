#include "local_acr/local_acr.h"

#include "audio/pcm_view.hpp"
#include "database/database_reader.hpp"
#include "session/event_codec.hpp"
#include "session/recognizer.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <vector>

struct lacr_recognizer final {
  std::mutex mutex;
  std::string database_path;
  std::uint32_t input_sample_rate = 0;
  local_acr::DatabaseReader reader;
  std::unique_ptr<local_acr::Recognizer> recognizer;
  std::optional<local_acr::RecognizerEvent> pending_event;
  std::uint64_t active_generation = 0;
  bool prepared = false;
  bool active = false;
};

namespace {

lacr_status_t from_status(const local_acr::Status status) noexcept {
  switch (status.code()) {
    case local_acr::StatusCode::Ok:
      return LACR_STATUS_OK;
    case local_acr::StatusCode::InvalidArgument:
      return LACR_STATUS_INVALID_ARGUMENT;
    case local_acr::StatusCode::ResourceLimitExceeded:
      return LACR_STATUS_RESOURCE_LIMIT_EXCEEDED;
    case local_acr::StatusCode::AudioDiscontinuity:
      return LACR_STATUS_AUDIO_DISCONTINUITY;
    case local_acr::StatusCode::InvalidState:
      return LACR_STATUS_INVALID_STATE;
    case local_acr::StatusCode::NativeEngineFailure:
      return LACR_STATUS_NATIVE_ENGINE_FAILURE;
  }
  return LACR_STATUS_NATIVE_ENGINE_FAILURE;
}

void write_error(lacr_error_buffer_t* error, const char* message) noexcept {
  if (error == nullptr) {
    return;
  }
  const std::size_t required = std::strlen(message) + 1U;
  error->required = required;
  if (error->data != nullptr && error->capacity != 0U) {
    const std::size_t count = std::min(error->capacity - 1U, required - 1U);
    std::memcpy(error->data, message, count);
    error->data[count] = '\0';
  }
}

bool valid_path(const char* path) noexcept {
  if (path == nullptr) {
    return false;
  }
  return std::strlen(path) < 4096U;
}

local_acr::SampleFormat sample_format(const lacr_sample_format_t format) noexcept {
  switch (format) {
    case LACR_S16_INTERLEAVED:
      return local_acr::SampleFormat::S16Interleaved;
    case LACR_F32_INTERLEAVED:
      return local_acr::SampleFormat::F32Interleaved;
    case LACR_F32_PLANAR:
      return local_acr::SampleFormat::F32Planar;
  }
  return local_acr::SampleFormat::S16Interleaved;
}

bool valid_pcm(const lacr_pcm_view_t* pcm) noexcept {
  if (pcm == nullptr || pcm->planes == nullptr || pcm->frames == 0U || pcm->channels == 0U) {
    return false;
  }
  if (pcm->format == LACR_F32_PLANAR) {
    return pcm->plane_count == pcm->channels;
  }
  return pcm->plane_count == 1U;
}

}  // namespace

extern "C" lacr_status_t lacr_recognizer_create(const char* database_path_utf8,
                                                 const lacr_config_t* config,
                                                 lacr_recognizer_t** out,
                                                 lacr_error_buffer_t* error) {
  try {
    if (!valid_path(database_path_utf8) || config == nullptr || out == nullptr ||
        config->abi_version != LACR_ABI_VERSION || config->input_sample_rate == 0U) {
      write_error(error, "invalid create argument");
      return LACR_STATUS_INVALID_ARGUMENT;
    }
    auto recognizer = std::make_unique<lacr_recognizer_t>();
    recognizer->database_path = database_path_utf8;
    recognizer->input_sample_rate = config->input_sample_rate;
    recognizer->recognizer = std::make_unique<local_acr::Recognizer>(config->input_sample_rate);
    *out = recognizer.release();
    return LACR_STATUS_OK;
  } catch (...) {
    write_error(error, "native exception");
    return LACR_STATUS_NATIVE_ENGINE_FAILURE;
  }
}

extern "C" lacr_status_t lacr_recognizer_prepare(lacr_recognizer_t* recognizer,
                                                  lacr_error_buffer_t* error) {
  try {
    if (recognizer == nullptr) {
      return LACR_STATUS_INVALID_ARGUMENT;
    }
    std::lock_guard lock(recognizer->mutex);
    const local_acr::Status open_status = recognizer->reader.open(recognizer->database_path);
    if (!open_status.ok()) {
      write_error(error, "database open failed");
      return from_status(open_status);
    }
    const local_acr::Status prepare_status = recognizer->recognizer->prepare(recognizer->reader);
    if (!prepare_status.ok()) {
      return from_status(prepare_status);
    }
    recognizer->prepared = true;
    return LACR_STATUS_OK;
  } catch (...) {
    write_error(error, "native exception");
    return LACR_STATUS_NATIVE_ENGINE_FAILURE;
  }
}

extern "C" lacr_status_t lacr_recognizer_start_session(lacr_recognizer_t* recognizer,
                                                        const uint64_t generation,
                                                        lacr_error_buffer_t* error) {
  try {
    (void)error;
    if (recognizer == nullptr) {
      return LACR_STATUS_INVALID_ARGUMENT;
    }
    std::lock_guard lock(recognizer->mutex);
    if (!recognizer->prepared) {
      return LACR_STATUS_INVALID_STATE;
    }
    const lacr_status_t status = from_status(recognizer->recognizer->start_session(generation));
    recognizer->active = status == LACR_STATUS_OK;
    recognizer->active_generation = recognizer->active ? generation : 0;
    return status;
  } catch (...) {
    write_error(error, "native exception");
    return LACR_STATUS_NATIVE_ENGINE_FAILURE;
  }
}

extern "C" lacr_status_t lacr_recognizer_push_pcm(lacr_recognizer_t* recognizer,
                                                   const uint64_t generation,
                                                   const lacr_pcm_view_t* pcm) {
  try {
    if (recognizer == nullptr || !valid_pcm(pcm)) {
      return LACR_STATUS_INVALID_ARGUMENT;
    }
    std::lock_guard lock(recognizer->mutex);
    if (!recognizer->active || recognizer->active_generation != generation) {
      return LACR_STATUS_INVALID_STATE;
    }
    if (pcm->plane_count > 8U) {
      return LACR_STATUS_INVALID_ARGUMENT;
    }
    std::array<local_acr::PcmPlane, 8> planes{};
    for (std::uint32_t i = 0; i < pcm->plane_count; ++i) {
      if (pcm->planes[i] == nullptr) {
        return LACR_STATUS_INVALID_ARGUMENT;
      }
      const std::size_t sample_size = pcm->format == LACR_S16_INTERLEAVED ? sizeof(std::int16_t) : sizeof(float);
      const std::size_t elements = pcm->format == LACR_F32_PLANAR
                                       ? static_cast<std::size_t>(pcm->frames)
                                       : static_cast<std::size_t>(pcm->frames) * pcm->channels;
      planes[i] = local_acr::PcmPlane{pcm->planes[i], elements * sample_size};
    }
    const local_acr::PcmView view{
        .planes = std::span<const local_acr::PcmPlane>(planes.data(), pcm->plane_count),
        .frames = pcm->frames,
        .channels = pcm->channels,
        .sample_rate = pcm->sample_rate,
        .first_source_frame = pcm->first_source_frame,
        .format = sample_format(pcm->format),
    };
    (void)generation;
    const lacr_status_t status = from_status(recognizer->recognizer->push_pcm(view));
    if (status != LACR_STATUS_OK) {
      recognizer->active = false;
      recognizer->active_generation = 0;
    }
    return status;
  } catch (...) {
    return LACR_STATUS_NATIVE_ENGINE_FAILURE;
  }
}

extern "C" lacr_status_t lacr_recognizer_poll_event(lacr_recognizer_t* recognizer,
                                                     lacr_event_t* event,
                                                     uint8_t* payload,
                                                     const size_t payload_capacity,
                                                     size_t* payload_required) {
  try {
    if (recognizer == nullptr || event == nullptr || payload_required == nullptr) {
      return LACR_STATUS_INVALID_ARGUMENT;
    }
    std::lock_guard lock(recognizer->mutex);
    std::optional<local_acr::RecognizerEvent> source = recognizer->pending_event;
    if (!source.has_value()) {
      source = recognizer->recognizer->poll_event();
    }
    if (!source.has_value()) {
      *payload_required = 0;
      return LACR_STATUS_NO_EVENT;
    }
    const local_acr::EncodedEvent encoded =
        local_acr::encode_event(*source, *event, payload, payload_capacity);
    *payload_required = encoded.required;
    if (encoded.status == LACR_STATUS_BUFFER_TOO_SMALL) {
      recognizer->pending_event = source;
      return encoded.status;
    }
    recognizer->pending_event.reset();
    return LACR_STATUS_OK;
  } catch (...) {
    return LACR_STATUS_NATIVE_ENGINE_FAILURE;
  }
}

extern "C" lacr_status_t lacr_recognizer_stop_session(lacr_recognizer_t* recognizer,
                                                       const uint64_t generation,
                                                       lacr_error_buffer_t* error) {
  try {
    (void)generation;
    (void)error;
    if (recognizer == nullptr) {
      return LACR_STATUS_INVALID_ARGUMENT;
    }
    std::lock_guard lock(recognizer->mutex);
    if (!recognizer->prepared || (recognizer->active && recognizer->active_generation != generation)) {
      return LACR_STATUS_INVALID_STATE;
    }
    recognizer->active = false;
    recognizer->active_generation = 0;
    return from_status(recognizer->recognizer->stop_session());
  } catch (...) {
    write_error(error, "native exception");
    return LACR_STATUS_NATIVE_ENGINE_FAILURE;
  }
}

extern "C" void lacr_recognizer_destroy(lacr_recognizer_t* recognizer) {
  delete recognizer;
}
