#include "local_acr/local_acr.h"

#include <atomic>
#include <cstdint>
#include <iostream>
#include <thread>

namespace {

int failures = 0;

void check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

void create_destroy_from_multiple_states() {
  lacr_config_t config{.abi_version = LACR_ABI_VERSION, .input_sample_rate = 11025};
  lacr_recognizer_t* recognizer = nullptr;
  check(lacr_recognizer_create("/tmp/missing.lacrdb", &config, &recognizer, nullptr) == LACR_STATUS_OK,
        "create succeeds for destroy test");
  lacr_recognizer_destroy(recognizer);

  recognizer = nullptr;
  check(lacr_recognizer_create("/tmp/missing.lacrdb", &config, &recognizer, nullptr) == LACR_STATUS_OK,
        "create succeeds for failed-state destroy test");
  check(lacr_recognizer_prepare(recognizer, nullptr) == LACR_STATUS_INVALID_ARGUMENT,
        "prepare enters failed state");
  lacr_recognizer_destroy(recognizer);
}

void poll_and_push_contracts_are_thread_safe_without_prepared_session() {
  lacr_config_t config{.abi_version = LACR_ABI_VERSION, .input_sample_rate = 11025};
  lacr_recognizer_t* recognizer = nullptr;
  check(lacr_recognizer_create("/tmp/missing.lacrdb", &config, &recognizer, nullptr) == LACR_STATUS_OK,
        "create succeeds for concurrency test");

  std::atomic<int> rejected_count = 0;
  std::thread producer([&] {
    for (int i = 0; i < 1000; ++i) {
      lacr_pcm_view_t pcm{};
      const lacr_status_t status = lacr_recognizer_push_pcm(recognizer, 1, &pcm);
      if (status == LACR_STATUS_INVALID_ARGUMENT || status == LACR_STATUS_INVALID_STATE) {
        rejected_count.fetch_add(1, std::memory_order_relaxed);
      }
    }
  });
  std::thread poller([&] {
    for (int i = 0; i < 1000; ++i) {
      lacr_event_t event{};
      size_t required = 0;
      (void)lacr_recognizer_poll_event(recognizer, &event, nullptr, 0, &required);
    }
  });
  producer.join();
  poller.join();
  check(rejected_count.load(std::memory_order_relaxed) == 1000, "pushes reject without session");
  lacr_recognizer_destroy(recognizer);
}

}  // namespace

int main() {
  create_destroy_from_multiple_states();
  poll_and_push_contracts_are_thread_safe_without_prepared_session();
  return failures == 0 ? 0 : 1;
}
