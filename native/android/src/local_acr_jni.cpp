#include <jni.h>

#include <local_acr/local_acr.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>

namespace {

// Slot layout shared with JniNativeBridge.decodePolledEvent in Kotlin.
constexpr jsize kPollSlotCount = 6;
constexpr std::size_t kPayloadCapacity = 512;

lacr_recognizer_t* from_handle(const jlong handle) noexcept {
  return reinterpret_cast<lacr_recognizer_t*>(static_cast<std::intptr_t>(handle));
}

std::size_t bytes_per_sample(const jint format) noexcept {
  return format == LACR_S16_INTERLEAVED ? sizeof(std::int16_t) : sizeof(float);
}

}  // namespace

extern "C" JNIEXPORT jlong JNICALL Java_com_localacr_android_JniNativeBridge_nativeCreate(
    JNIEnv* env, jobject /*thiz*/, jstring database_path, jint sample_rate) {
  if (database_path == nullptr || sample_rate <= 0) {
    return 0;
  }
  const char* path = env->GetStringUTFChars(database_path, nullptr);
  if (path == nullptr) {
    return 0;
  }
  const lacr_config_t config{LACR_ABI_VERSION, static_cast<std::uint32_t>(sample_rate)};
  lacr_recognizer_t* recognizer = nullptr;
  const lacr_status_t status = lacr_recognizer_create(path, &config, &recognizer, nullptr);
  env->ReleaseStringUTFChars(database_path, path);
  if (status != LACR_STATUS_OK) {
    return 0;
  }
  return static_cast<jlong>(reinterpret_cast<std::intptr_t>(recognizer));
}

extern "C" JNIEXPORT jint JNICALL Java_com_localacr_android_JniNativeBridge_nativePrepare(
    JNIEnv* /*env*/, jobject /*thiz*/, jlong handle) {
  return static_cast<jint>(lacr_recognizer_prepare(from_handle(handle), nullptr));
}

extern "C" JNIEXPORT jint JNICALL Java_com_localacr_android_JniNativeBridge_nativeStartSession(
    JNIEnv* /*env*/, jobject /*thiz*/, jlong handle, jlong generation) {
  return static_cast<jint>(
      lacr_recognizer_start_session(from_handle(handle), static_cast<std::uint64_t>(generation), nullptr));
}

extern "C" JNIEXPORT jint JNICALL Java_com_localacr_android_JniNativeBridge_nativePushPcm(
    JNIEnv* env, jobject /*thiz*/, jlong handle, jlong generation, jobject direct_buffer, jint frames,
    jint channels, jint sample_rate, jlong first_source_frame, jint format) {
  if (direct_buffer == nullptr || frames <= 0 || channels <= 0 || sample_rate <= 0 || first_source_frame < 0 ||
      format < LACR_S16_INTERLEAVED || format > LACR_F32_PLANAR) {
    return LACR_STATUS_INVALID_ARGUMENT;
  }
  void* data = env->GetDirectBufferAddress(direct_buffer);
  const jlong capacity = env->GetDirectBufferCapacity(direct_buffer);
  const std::size_t needed =
      static_cast<std::size_t>(frames) * static_cast<std::size_t>(channels) * bytes_per_sample(format);
  if (data == nullptr || capacity < 0 || static_cast<std::size_t>(capacity) < needed) {
    return LACR_STATUS_INVALID_ARGUMENT;
  }
  const void* planes[1] = {data};
  const lacr_pcm_view_t view{
      planes,
      1U,
      static_cast<std::uint32_t>(frames),
      static_cast<std::uint32_t>(channels),
      static_cast<std::uint32_t>(sample_rate),
      static_cast<std::uint64_t>(first_source_frame),
      static_cast<lacr_sample_format_t>(format),
  };
  return static_cast<jint>(
      lacr_recognizer_push_pcm(from_handle(handle), static_cast<std::uint64_t>(generation), &view));
}

extern "C" JNIEXPORT jstring JNICALL Java_com_localacr_android_JniNativeBridge_nativePollEvent(
    JNIEnv* env, jobject /*thiz*/, jlong handle, jlongArray out_slots) {
  if (out_slots == nullptr || env->GetArrayLength(out_slots) < kPollSlotCount) {
    return nullptr;
  }
  lacr_event_t event{};
  std::array<std::uint8_t, kPayloadCapacity> payload{};
  std::size_t required = 0;
  const lacr_status_t status =
      lacr_recognizer_poll_event(from_handle(handle), &event, payload.data(), payload.size(), &required);

  std::array<jlong, kPollSlotCount> slots{static_cast<jlong>(status), 0, 0, 0, 0, 0};
  if (status == LACR_STATUS_OK) {
    slots[1] = static_cast<jlong>(event.type);
    slots[2] = static_cast<jlong>(event.error);
    slots[3] = static_cast<jlong>(event.matched_cue_time_frame);
    slots[4] = static_cast<jlong>(std::lround(static_cast<double>(event.confidence) * 1000.0));
    slots[5] = static_cast<jlong>(event.aligned_count);
  }
  env->SetLongArrayRegion(out_slots, 0, kPollSlotCount, slots.data());

  if (status != LACR_STATUS_OK || event.type != LACR_EVENT_RECOGNITION) {
    return nullptr;
  }
  const std::string trigger_id(reinterpret_cast<const char*>(payload.data()) + event.trigger_id_offset,
                               event.trigger_id_length);
  return env->NewStringUTF(trigger_id.c_str());
}

extern "C" JNIEXPORT jint JNICALL Java_com_localacr_android_JniNativeBridge_nativeStopSession(
    JNIEnv* /*env*/, jobject /*thiz*/, jlong handle, jlong generation) {
  return static_cast<jint>(
      lacr_recognizer_stop_session(from_handle(handle), static_cast<std::uint64_t>(generation), nullptr));
}

extern "C" JNIEXPORT void JNICALL Java_com_localacr_android_JniNativeBridge_nativeDestroy(
    JNIEnv* /*env*/, jobject /*thiz*/, jlong handle) {
  lacr_recognizer_destroy(from_handle(handle));
}
