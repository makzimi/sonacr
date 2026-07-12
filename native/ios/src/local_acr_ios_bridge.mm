#include "local_acr_ios_bridge.h"

#import <AVFoundation/AVFoundation.h>
#import <Foundation/Foundation.h>
#import <TargetConditionals.h>

#include <atomic>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <memory>

struct lacr_ios_capture {
  lacr_recognizer_t *recognizer = nullptr;
  uint64_t generation = 0;
  uint32_t preferred_sample_rate = 0;
  AVAudioEngine *engine = nil;
  std::array<const void *, 32> planes{};
  std::atomic<uint64_t> next_source_frame{0};
  std::atomic<bool> running{false};
};

namespace {

lacr_ios_capture_status_t configure_audio_session(uint32_t preferred_sample_rate) {
#if TARGET_OS_IOS || TARGET_OS_TV || TARGET_OS_WATCH
  AVAudioSession *session = [AVAudioSession sharedInstance];
  NSError *error = nil;
  if (![session setCategory:AVAudioSessionCategoryRecord
                 withOptions:AVAudioSessionCategoryOptionDuckOthers
                       error:&error]) {
    return LACR_IOS_CAPTURE_MICROPHONE_UNAVAILABLE;
  }
  if (preferred_sample_rate != 0U &&
      ![session setPreferredSampleRate:static_cast<double>(preferred_sample_rate) error:&error]) {
    return LACR_IOS_CAPTURE_MICROPHONE_UNAVAILABLE;
  }
  if (![session setActive:YES error:&error]) {
    return LACR_IOS_CAPTURE_MICROPHONE_PERMISSION_DENIED;
  }
#else
  (void)preferred_sample_rate;
#endif
  return LACR_IOS_CAPTURE_OK;
}

void deactivate_audio_session() {
#if TARGET_OS_IOS || TARGET_OS_TV || TARGET_OS_WATCH
  [[AVAudioSession sharedInstance] setActive:NO
                                 withOptions:AVAudioSessionSetActiveOptionNotifyOthersOnDeactivation
                                       error:nil];
#endif
}

lacr_sample_format_t sample_format(AVAudioCommonFormat format) {
  switch (format) {
    case AVAudioPCMFormatFloat32:
      return LACR_F32_PLANAR;
    case AVAudioPCMFormatInt16:
      return LACR_S16_INTERLEAVED;
    default:
      return static_cast<lacr_sample_format_t>(0);
  }
}

void push_buffer(lacr_ios_capture *capture, AVAudioPCMBuffer *buffer) {
  if (capture == nullptr || capture->recognizer == nullptr || buffer == nil) {
    return;
  }
  AVAudioFormat *format = buffer.format;
  const AVAudioFrameCount frame_length = buffer.frameLength;
  const AVAudioChannelCount channel_count = format.channelCount;
  if (frame_length == 0U || channel_count == 0U) {
    return;
  }

  uint32_t plane_count = 0;
  lacr_sample_format_t lacr_format = sample_format(format.commonFormat);
  if (format.commonFormat == AVAudioPCMFormatFloat32 && buffer.floatChannelData != nullptr) {
    if (channel_count > capture->planes.size()) {
      return;
    }
    for (AVAudioChannelCount channel = 0; channel < channel_count; ++channel) {
      capture->planes[channel] = buffer.floatChannelData[channel];
    }
    plane_count = static_cast<uint32_t>(channel_count);
    lacr_format = LACR_F32_PLANAR;
  } else if (format.commonFormat == AVAudioPCMFormatInt16 && buffer.int16ChannelData != nullptr) {
    capture->planes[0] = buffer.int16ChannelData[0];
    plane_count = 1;
    lacr_format = format.isInterleaved ? LACR_S16_INTERLEAVED : static_cast<lacr_sample_format_t>(0);
  }

  if (plane_count == 0U || lacr_format == 0) {
    return;
  }

  const uint64_t first_frame = capture->next_source_frame.fetch_add(frame_length, std::memory_order_relaxed);
  const lacr_pcm_view_t view = {
      .planes = capture->planes.data(),
      .plane_count = plane_count,
      .frames = static_cast<uint32_t>(frame_length),
      .channels = static_cast<uint32_t>(channel_count),
      .sample_rate = static_cast<uint32_t>(format.sampleRate),
      .first_source_frame = first_frame,
      .format = lacr_format,
  };
  (void)lacr_recognizer_push_pcm(capture->recognizer, capture->generation, &view);
}

}  // namespace

extern "C" lacr_ios_capture_status_t lacr_ios_capture_create(
    lacr_recognizer_t *recognizer,
    uint64_t generation,
    uint32_t preferred_sample_rate,
    lacr_ios_capture_t **out_capture) {
  if (recognizer == nullptr || out_capture == nullptr) {
    return LACR_IOS_CAPTURE_INVALID_ARGUMENT;
  }
  *out_capture = nullptr;
  auto capture = std::make_unique<lacr_ios_capture>();
  capture->recognizer = recognizer;
  capture->generation = generation;
  capture->preferred_sample_rate = preferred_sample_rate;
  capture->engine = [[AVAudioEngine alloc] init];
  if (capture->engine == nil) {
    return LACR_IOS_CAPTURE_AUDIO_ENGINE_FAILURE;
  }
  *out_capture = capture.release();
  return LACR_IOS_CAPTURE_OK;
}

extern "C" lacr_recognizer_t *lacr_ios_recognizer_create_prepared(
    const char *database_path_utf8,
    uint32_t input_sample_rate,
    lacr_status_t *out_status) {
  if (out_status != nullptr) {
    *out_status = LACR_STATUS_INVALID_ARGUMENT;
  }
  lacr_config_t config = {
      .abi_version = LACR_ABI_VERSION,
      .input_sample_rate = input_sample_rate,
  };
  lacr_recognizer_t *recognizer = nullptr;
  lacr_status_t status = lacr_recognizer_create(database_path_utf8, &config, &recognizer, nullptr);
  if (status == LACR_STATUS_OK) {
    status = lacr_recognizer_prepare(recognizer, nullptr);
  }
  if (status != LACR_STATUS_OK && recognizer != nullptr) {
    lacr_recognizer_destroy(recognizer);
    recognizer = nullptr;
  }
  if (out_status != nullptr) {
    *out_status = status;
  }
  return recognizer;
}

extern "C" lacr_ios_capture_t *lacr_ios_capture_create_handle(
    lacr_recognizer_t *recognizer,
    uint64_t generation,
    uint32_t preferred_sample_rate,
    lacr_ios_capture_status_t *out_status) {
  if (out_status != nullptr) {
    *out_status = LACR_IOS_CAPTURE_INVALID_ARGUMENT;
  }
  lacr_ios_capture_t *capture = nullptr;
  const lacr_ios_capture_status_t status =
      lacr_ios_capture_create(recognizer, generation, preferred_sample_rate, &capture);
  if (out_status != nullptr) {
    *out_status = status;
  }
  return capture;
}

extern "C" lacr_ios_capture_status_t lacr_ios_capture_start(lacr_ios_capture_t *capture) {
  if (capture == nullptr || capture->engine == nil) {
    return LACR_IOS_CAPTURE_INVALID_ARGUMENT;
  }
  if (capture->running.exchange(true, std::memory_order_acq_rel)) {
    return LACR_IOS_CAPTURE_OK;
  }

  lacr_ios_capture_status_t session_status = configure_audio_session(capture->preferred_sample_rate);
  if (session_status != LACR_IOS_CAPTURE_OK) {
    capture->running.store(false, std::memory_order_release);
    return session_status;
  }

  AVAudioInputNode *input = capture->engine.inputNode;
  AVAudioFormat *format = [input outputFormatForBus:0];
  [input installTapOnBus:0
              bufferSize:4096
                  format:format
                   block:^(AVAudioPCMBuffer *buffer, AVAudioTime *when) {
                     (void)when;
                     push_buffer(capture, buffer);
                   }];

  NSError *error = nil;
  if (![capture->engine startAndReturnError:&error]) {
    [input removeTapOnBus:0];
    capture->running.store(false, std::memory_order_release);
    deactivate_audio_session();
    return LACR_IOS_CAPTURE_AUDIO_ENGINE_FAILURE;
  }
  return LACR_IOS_CAPTURE_OK;
}

extern "C" void lacr_ios_capture_stop(lacr_ios_capture_t *capture) {
  if (capture == nullptr || capture->engine == nil) {
    return;
  }
  if (!capture->running.exchange(false, std::memory_order_acq_rel)) {
    return;
  }
  [capture->engine.inputNode removeTapOnBus:0];
  [capture->engine stop];
  deactivate_audio_session();
}

extern "C" void lacr_ios_capture_destroy(lacr_ios_capture_t *capture) {
  if (capture == nullptr) {
    return;
  }
  lacr_ios_capture_stop(capture);
  delete capture;
}
