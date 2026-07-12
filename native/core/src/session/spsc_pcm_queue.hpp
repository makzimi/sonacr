#ifndef LOCAL_ACR_SESSION_SPSC_PCM_QUEUE_HPP
#define LOCAL_ACR_SESSION_SPSC_PCM_QUEUE_HPP

#include "support/status.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace local_acr {

struct QueuedPcmChunk final {
  std::uint64_t generation = 0;
  std::vector<std::uint8_t> bytes;
};

class SpscPcmQueue final {
 public:
  explicit SpscPcmQueue(std::size_t capacity);

  [[nodiscard]] Status try_push(QueuedPcmChunk chunk);
  [[nodiscard]] std::optional<QueuedPcmChunk> try_pop();
  [[nodiscard]] std::size_t size() const noexcept;
  [[nodiscard]] std::size_t capacity() const noexcept;

 private:
  std::vector<QueuedPcmChunk> ring_;
  std::size_t capacity_ = 0;
  std::atomic<std::size_t> head_{0};
  std::atomic<std::size_t> tail_{0};
};

}  // namespace local_acr

#endif
