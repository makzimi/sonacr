#include <local_acr/local_acr.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

enum { kChunkFrames = 1024, kPayloadCapacity = 512 };

static unsigned long long frames_to_ms(uint64_t frames, uint32_t sample_rate) {
  return (unsigned long long)(frames * 1000u / sample_rate);
}

static void drain_events(lacr_recognizer_t *recognizer, uint64_t pushed_frames, uint32_t sample_rate) {
  for (;;) {
    lacr_event_t event;
    uint8_t payload[kPayloadCapacity];
    size_t required = 0;
    if (lacr_recognizer_poll_event(recognizer, &event, payload, sizeof payload, &required) != LACR_STATUS_OK) {
      return;
    }
    if (event.type == LACR_EVENT_RECOGNITION) {
      printf("{\"event\":\"recognized\",\"triggerId\":\"%.*s\",\"confidence\":%.3f,"
             "\"alignedCount\":%u,\"atMs\":%llu}\n",
             (int)event.trigger_id_length, (const char *)payload + event.trigger_id_offset,
             (double)event.confidence, event.aligned_count, frames_to_ms(pushed_frames, sample_rate));
    } else {
      printf("{\"event\":\"sessionError\",\"error\":%d,\"atMs\":%llu}\n", (int)event.error,
             frames_to_ms(pushed_frames, sample_rate));
    }
  }
}

int main(int argc, char **argv) {
  if (argc != 4) {
    fprintf(stderr, "usage: local_acr_probe <database.lacrdb> <pcm_s16le_mono.raw> <sample_rate>\n");
    return 2;
  }
  const long rate_arg = strtol(argv[3], NULL, 10);
  if (rate_arg <= 0 || rate_arg > 192000) {
    fprintf(stderr, "invalid sample rate\n");
    return 2;
  }
  const uint32_t sample_rate = (uint32_t)rate_arg;
  FILE *input = fopen(argv[2], "rb");
  if (input == NULL) {
    fprintf(stderr, "cannot open pcm input\n");
    return 2;
  }

  char error_text[256] = {0};
  lacr_error_buffer_t error = {error_text, sizeof error_text, 0};
  const lacr_config_t config = {LACR_ABI_VERSION, sample_rate};
  lacr_recognizer_t *recognizer = NULL;
  if (lacr_recognizer_create(argv[1], &config, &recognizer, &error) != LACR_STATUS_OK ||
      lacr_recognizer_prepare(recognizer, &error) != LACR_STATUS_OK ||
      lacr_recognizer_start_session(recognizer, 1, &error) != LACR_STATUS_OK) {
    fprintf(stderr, "recognizer setup failed: %s\n", error_text);
    fclose(input);
    lacr_recognizer_destroy(recognizer);
    return 1;
  }

  int16_t chunk[kChunkFrames];
  uint64_t pushed = 0;
  int exit_code = 0;
  for (;;) {
    const size_t frames = fread(chunk, sizeof chunk[0], kChunkFrames, input);
    if (frames == 0) {
      break;
    }
    const void *planes[1] = {chunk};
    const lacr_pcm_view_t view = {planes, 1, (uint32_t)frames, 1, sample_rate, pushed, LACR_S16_INTERLEAVED};
    const lacr_status_t status = lacr_recognizer_push_pcm(recognizer, 1, &view);
    pushed += frames;
    drain_events(recognizer, pushed, sample_rate);
    if (status != LACR_STATUS_OK) {
      printf("{\"event\":\"pushFailed\",\"status\":%d,\"atMs\":%llu}\n", (int)status,
             frames_to_ms(pushed, sample_rate));
      exit_code = 1;
      break;
    }
  }
  printf("{\"event\":\"end\",\"durationMs\":%llu}\n", frames_to_ms(pushed, sample_rate));
  fclose(input);
  lacr_recognizer_stop_session(recognizer, 1, &error);
  lacr_recognizer_destroy(recognizer);
  return exit_code;
}
