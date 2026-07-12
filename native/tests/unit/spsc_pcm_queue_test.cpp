#include "session/spsc_pcm_queue.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <thread>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

void exact_capacity_and_overflow_do_not_block() {
  local_acr::SpscPcmQueue queue(3);
  check(queue.try_push(local_acr::QueuedPcmChunk{.generation = 1, .bytes = {1, 2, 3}}).ok(),
        "push first chunk");
  check(queue.try_push(local_acr::QueuedPcmChunk{.generation = 1, .bytes = {4}}).ok(),
        "push second chunk");
  check(queue.try_push(local_acr::QueuedPcmChunk{.generation = 1, .bytes = {5}}).ok(),
        "push third chunk at exact capacity");
  check(queue.try_push(local_acr::QueuedPcmChunk{.generation = 1, .bytes = {6}}).code() ==
            local_acr::StatusCode::ResourceLimitExceeded,
        "overflow is reported without blocking");

  auto first = queue.try_pop();
  check(first.has_value() && first->bytes.size() == 3U && first->bytes[0] == 1U,
        "consumer sees first chunk");
  check(queue.try_push(local_acr::QueuedPcmChunk{.generation = 2, .bytes = {7, 8}}).ok(),
        "wraparound push succeeds after pop");
}

void consumer_order_is_fifo_across_wraparound() {
  local_acr::SpscPcmQueue queue(2);
  check(queue.try_push(local_acr::QueuedPcmChunk{.generation = 1, .bytes = {1}}).ok(), "push 1");
  check(queue.try_push(local_acr::QueuedPcmChunk{.generation = 2, .bytes = {2}}).ok(), "push 2");
  auto first = queue.try_pop();
  check(first.has_value() && first->generation == 1, "pop generation 1");
  check(queue.try_push(local_acr::QueuedPcmChunk{.generation = 3, .bytes = {3}}).ok(), "push 3");
  auto second = queue.try_pop();
  auto third = queue.try_pop();
  check(second.has_value() && second->generation == 2, "pop generation 2");
  check(third.has_value() && third->generation == 3, "pop generation 3");
  check(!queue.try_pop().has_value(), "empty queue returns no chunk");
}

void producer_consumer_loop_preserves_visibility_under_tsan() {
  constexpr std::uint32_t count = 2000;
  local_acr::SpscPcmQueue queue(64);
  std::atomic<bool> done = false;
  std::atomic<std::uint64_t> sum = 0;

  std::thread producer([&] {
    for (std::uint32_t i = 1; i <= count; ++i) {
      local_acr::QueuedPcmChunk chunk{.generation = i, .bytes = {static_cast<std::uint8_t>(i & 0xFFU)}};
      while (!queue.try_push(std::move(chunk)).ok()) {
        std::this_thread::yield();
        chunk = local_acr::QueuedPcmChunk{.generation = i, .bytes = {static_cast<std::uint8_t>(i & 0xFFU)}};
      }
    }
    done.store(true, std::memory_order_release);
  });

  std::uint32_t expected = 1;
  while (!done.load(std::memory_order_acquire) || queue.size() != 0U) {
    auto chunk = queue.try_pop();
    if (!chunk.has_value()) {
      std::this_thread::yield();
      continue;
    }
    check(chunk->generation == expected, "consumer order in threaded loop");
    sum.fetch_add(chunk->generation, std::memory_order_relaxed);
    ++expected;
  }
  producer.join();
  check(expected == count + 1U, "consumer received every chunk");
  check(sum.load(std::memory_order_relaxed) == (static_cast<std::uint64_t>(count) * (count + 1ULL)) / 2ULL,
        "consumer observed initialized payloads");
}

}  // namespace

int main() {
  exact_capacity_and_overflow_do_not_block();
  consumer_order_is_fifo_across_wraparound();
  producer_consumer_loop_preserves_visibility_under_tsan();
  return failures == 0 ? 0 : 1;
}
