#include "local_acr/local_acr.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;

static void check(int condition, const char *message) {
  if (!condition) {
    fprintf(stderr, "FAIL: %s\n", message);
    ++failures;
  }
}

static void null_and_state_validation(void) {
  lacr_error_buffer_t error = {.data = NULL, .capacity = 0, .required = 0};
  lacr_recognizer_t *recognizer = NULL;
  lacr_config_t config = {.abi_version = LACR_ABI_VERSION, .input_sample_rate = 11025};

  check(lacr_recognizer_create(NULL, &config, &recognizer, &error) == LACR_STATUS_INVALID_ARGUMENT,
        "create rejects null path");
  check(lacr_recognizer_create("missing-nul", &config, NULL, &error) == LACR_STATUS_INVALID_ARGUMENT,
        "create rejects null out");
  check(lacr_recognizer_prepare(NULL, &error) == LACR_STATUS_INVALID_ARGUMENT,
        "prepare rejects null handle");
  check(lacr_recognizer_start_session(NULL, 1, &error) == LACR_STATUS_INVALID_ARGUMENT,
        "start rejects null handle");
  check(lacr_recognizer_push_pcm(NULL, 1, NULL) == LACR_STATUS_INVALID_ARGUMENT,
        "push rejects null handle");
  check(lacr_recognizer_poll_event(NULL, NULL, NULL, 0, NULL) == LACR_STATUS_INVALID_ARGUMENT,
        "poll rejects null handle");
  lacr_recognizer_destroy(NULL);
}

static void create_prepare_and_event_buffer_retry(void) {
  lacr_error_buffer_t error = {.data = NULL, .capacity = 0, .required = 0};
  lacr_recognizer_t *recognizer = NULL;
  lacr_config_t config = {.abi_version = LACR_ABI_VERSION, .input_sample_rate = 11025};

  check(lacr_recognizer_create("/tmp/does-not-exist.lacrdb", &config, &recognizer, &error) ==
            LACR_STATUS_OK,
        "create accepts copied path");
  check(recognizer != NULL, "create fills recognizer");
  check(lacr_recognizer_prepare(recognizer, &error) == LACR_STATUS_INVALID_ARGUMENT,
        "prepare reports invalid database path");
  check(lacr_recognizer_start_session(recognizer, 7, &error) == LACR_STATUS_INVALID_STATE,
        "start before prepare rejects");

  lacr_event_t event;
  uint8_t tiny[1];
  size_t required = 0;
  check(lacr_recognizer_poll_event(recognizer, &event, tiny, sizeof(tiny), &required) ==
            LACR_STATUS_NO_EVENT,
        "empty poll reports no event");
  lacr_recognizer_destroy(recognizer);
}

static void header_structs_are_c_usable(void) {
  lacr_pcm_view_t pcm;
  memset(&pcm, 0, sizeof(pcm));
  pcm.format = LACR_F32_INTERLEAVED;
  check(sizeof(lacr_event_t) >= 32U, "event header is fixed width");
  check(pcm.format == LACR_F32_INTERLEAVED, "sample format enum is usable from C");
}

int main(void) {
  null_and_state_validation();
  create_prepare_and_event_buffer_retry();
  header_structs_are_c_usable();
  return failures == 0 ? 0 : 1;
}
