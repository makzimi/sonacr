#ifndef LOCAL_ACR_IOS_BRIDGE_H
#define LOCAL_ACR_IOS_BRIDGE_H

#include <stdint.h>

#include "local_acr/local_acr.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct lacr_ios_capture lacr_ios_capture_t;

typedef enum lacr_ios_capture_status {
  LACR_IOS_CAPTURE_OK = 0,
  LACR_IOS_CAPTURE_INVALID_ARGUMENT = 1,
  LACR_IOS_CAPTURE_MICROPHONE_PERMISSION_DENIED = 2,
  LACR_IOS_CAPTURE_MICROPHONE_UNAVAILABLE = 3,
  LACR_IOS_CAPTURE_AUDIO_ENGINE_FAILURE = 4
} lacr_ios_capture_status_t;

lacr_ios_capture_status_t lacr_ios_capture_create(
    lacr_recognizer_t *recognizer,
    uint64_t generation,
    uint32_t preferred_sample_rate,
    lacr_ios_capture_t **out_capture);

lacr_recognizer_t *lacr_ios_recognizer_create_prepared(
    const char *database_path_utf8,
    uint32_t input_sample_rate,
    lacr_status_t *out_status);

lacr_ios_capture_t *lacr_ios_capture_create_handle(
    lacr_recognizer_t *recognizer,
    uint64_t generation,
    uint32_t preferred_sample_rate,
    lacr_ios_capture_status_t *out_status);

lacr_ios_capture_status_t lacr_ios_capture_start(lacr_ios_capture_t *capture);

void lacr_ios_capture_stop(lacr_ios_capture_t *capture);

void lacr_ios_capture_destroy(lacr_ios_capture_t *capture);

#ifdef __cplusplus
}
#endif

#endif
