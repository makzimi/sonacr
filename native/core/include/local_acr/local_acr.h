#ifndef LOCAL_ACR_LOCAL_ACR_H
#define LOCAL_ACR_LOCAL_ACR_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LACR_ABI_VERSION 1u

typedef struct lacr_recognizer lacr_recognizer_t;

typedef enum lacr_status {
  LACR_STATUS_OK = 0,
  LACR_STATUS_INVALID_ARGUMENT = 1,
  LACR_STATUS_RESOURCE_LIMIT_EXCEEDED = 2,
  LACR_STATUS_AUDIO_DISCONTINUITY = 3,
  LACR_STATUS_INVALID_STATE = 4,
  LACR_STATUS_NATIVE_ENGINE_FAILURE = 5,
  LACR_STATUS_BUFFER_TOO_SMALL = 6,
  LACR_STATUS_NO_EVENT = 7
} lacr_status_t;

typedef enum lacr_sample_format {
  LACR_S16_INTERLEAVED = 1,
  LACR_F32_INTERLEAVED = 2,
  LACR_F32_PLANAR = 3
} lacr_sample_format_t;

typedef enum lacr_event_type {
  LACR_EVENT_RECOGNITION = 1,
  LACR_EVENT_SESSION_ERROR = 2
} lacr_event_type_t;

typedef enum lacr_error_code {
  LACR_ERROR_NONE = 0,
  LACR_ERROR_QUERY_DENSITY_EXCEEDED = 1,
  LACR_ERROR_AUDIO_DISCONTINUITY = 2,
  LACR_ERROR_NATIVE_ENGINE_FAILURE = 3
} lacr_error_code_t;

typedef struct lacr_config {
  uint32_t abi_version;
  uint32_t input_sample_rate;
} lacr_config_t;

typedef struct lacr_error_buffer {
  char *data;
  size_t capacity;
  size_t required;
} lacr_error_buffer_t;

typedef struct lacr_pcm_view {
  const void *const *planes;
  uint32_t plane_count;
  uint32_t frames;
  uint32_t channels;
  uint32_t sample_rate;
  uint64_t first_source_frame;
  lacr_sample_format_t format;
} lacr_pcm_view_t;

typedef struct lacr_event {
  uint32_t abi_version;
  lacr_event_type_t type;
  lacr_error_code_t error;
  uint64_t generation;
  uint64_t newest_source_frame;
  uint32_t matched_cue_time_frame;
  uint32_t aligned_count;
  float confidence;
  float aligned_ratio;
  uint32_t trigger_id_offset;
  uint32_t trigger_id_length;
} lacr_event_t;

uint32_t lacr_version_abi(void);

lacr_status_t lacr_recognizer_create(
    const char *database_path_utf8,
    const lacr_config_t *config,
    lacr_recognizer_t **out,
    lacr_error_buffer_t *error);

lacr_status_t lacr_recognizer_prepare(
    lacr_recognizer_t *recognizer,
    lacr_error_buffer_t *error);

lacr_status_t lacr_recognizer_start_session(
    lacr_recognizer_t *recognizer,
    uint64_t generation,
    lacr_error_buffer_t *error);

lacr_status_t lacr_recognizer_push_pcm(
    lacr_recognizer_t *recognizer,
    uint64_t generation,
    const lacr_pcm_view_t *pcm);

lacr_status_t lacr_recognizer_poll_event(
    lacr_recognizer_t *recognizer,
    lacr_event_t *event,
    uint8_t *payload,
    size_t payload_capacity,
    size_t *payload_required);

lacr_status_t lacr_recognizer_stop_session(
    lacr_recognizer_t *recognizer,
    uint64_t generation,
    lacr_error_buffer_t *error);

void lacr_recognizer_destroy(lacr_recognizer_t *recognizer);

#ifdef __cplusplus
}
#endif

#endif
