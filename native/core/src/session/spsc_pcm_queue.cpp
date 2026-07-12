#include "session/spsc_pcm_queue.hpp"

#include <cstddef>
#include <optional>
#include <utility>

namespace local_acr {

SpscPcmQueue::SpscPcmQueue(const std::size_t capacity) : ring_(capacity + 1U), capacity_(capacity) {}

Status SpscPcmQueue::try_push(QueuedPcmChunk chunk) {
  if (capacity_ == 0U) {
    return Status::resource_limit_exceeded();
  }
  const std::size_t tail = tail_.load(std::memory_order_relaxed);
  const std::size_t next = (tail + 1U) % ring_.size();
  if (next == head_.load(std::memory_order_acquire)) {
    return Status::resource_limit_exceeded();
  }
  ring_[tail] = std::move(chunk);
  tail_.store(next, std::memory_order_release);
  return Status::ok_status();
}

std::optional<QueuedPcmChunk> SpscPcmQueue::try_pop() {
  const std::size_t head = head_.load(std::memory_order_relaxed);
  if (head == tail_.load(std::memory_order_acquire)) {
    return std::nullopt;
  }
  QueuedPcmChunk chunk = std::move(ring_[head]);
  head_.store((head + 1U) % ring_.size(), std::memory_order_release);
  return chunk;
}

std::size_t SpscPcmQueue::size() const noexcept {
  const std::size_t head = head_.load(std::memory_order_acquire);
  const std::size_t tail = tail_.load(std::memory_order_acquire);
  if (tail >= head) {
    return tail - head;
  }
  return ring_.size() - head + tail;
}

std::size_t SpscPcmQueue::capacity() const noexcept {
  return capacity_;
}

}  // namespace local_acr
