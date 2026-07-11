#include "audio/frame_stream.hpp"
#include "fingerprint/hann_q31.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

namespace {

using local_acr::AnalysisFrame;
using local_acr::AnalysisFrameSink;
using local_acr::FrameStream;
using local_acr::Status;
using local_acr::StatusCode;

int failures = 0;

void check(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

void check_equal(float actual, float expected, std::string_view message) {
  check(std::bit_cast<std::uint32_t>(actual) == std::bit_cast<std::uint32_t>(expected), message);
}

class CapturingFrameSink final : public AnalysisFrameSink {
 public:
  Status consume(const AnalysisFrame& frame) noexcept override {
    frames.push_back(frame);
    return Status::ok_status();
  }

  std::vector<AnalysisFrame> frames;
};

std::vector<float> make_canonical_samples(std::size_t count) {
  std::vector<float> samples(count);
  for (std::size_t index = 0; index < count; ++index) {
    const std::uint32_t pattern = (static_cast<std::uint32_t>(index) * 53U + 19U) % 1021U;
    const std::int32_t q1_23 = static_cast<std::int32_t>(pattern) - 510;
    samples[index] = static_cast<float>(q1_23) / 8388608.0F;
  }
  return samples;
}

std::vector<AnalysisFrame> run_partitioned(std::span<const float> samples,
                                           std::span<const std::size_t> chunks) {
  FrameStream stream;
  CapturingFrameSink sink;
  std::size_t offset = 0;
  for (const std::size_t chunk : chunks) {
    check(chunk > 0 && chunk <= samples.size() - offset, "frame chunk is in range");
    if (chunk == 0 || chunk > samples.size() - offset) {
      return {};
    }
    check(stream.push(samples.subspan(offset, chunk), sink).ok(), "frame chunk succeeds");
    offset += chunk;
  }
  check(offset == samples.size(), "frame chunks cover input");
  check(stream.finish().ok(), "frame finite finish succeeds");
  return sink.frames;
}

std::vector<AnalysisFrame> run_one_batch(std::span<const float> samples) {
  const std::array chunks{samples.size()};
  return run_partitioned(samples, chunks);
}

void hann_table_is_checked_and_symmetric() {
  check(local_acr::kHannQ31.size() == FrameStream::kWindowSize, "Hann coefficient count");
  check(local_acr::kHannQ31.front() == 0, "Hann first endpoint is zero");
  check(local_acr::kHannQ31.back() == 0, "Hann last endpoint is zero");
  check(local_acr::kHannQ31[255] == local_acr::kHannQ31[256],
        "Hann center pair is symmetric");
  check(local_acr::kHannQ31[255] == 2147463356, "Hann center coefficient is pinned");

  std::uint64_t checksum = 1469598103934665603ULL;
  for (std::size_t index = 0; index < local_acr::kHannQ31.size(); ++index) {
    check(local_acr::kHannQ31[index] >= 0, "Hann coefficient is nonnegative");
    check(local_acr::kHannQ31[index] == local_acr::kHannQ31[511 - index],
          "Hann coefficients are symmetric");
    const std::uint32_t value = static_cast<std::uint32_t>(local_acr::kHannQ31[index]);
    for (std::size_t byte = 0; byte < 4; ++byte) {
      checksum ^= (value >> (byte * 8U)) & 0xFFU;
      checksum *= 1099511628211ULL;
    }
  }
  check(checksum == 0xFD32B28E51840123ULL, "Hann table checksum is pinned");
}

void incomplete_windows_are_not_emitted() {
  const std::vector<float> samples = make_canonical_samples(511);
  const std::vector<AnalysisFrame> frames = run_one_batch(samples);
  check(frames.empty(), "511 samples do not emit a frame");
}

void complete_windows_start_at_exact_hops() {
  const std::vector<float> samples = make_canonical_samples(640);
  const std::vector<AnalysisFrame> frames = run_one_batch(samples);
  check(frames.size() == 2, "640 samples emit two frames");
  if (frames.size() != 2) {
    return;
  }

  check(frames[0].time_frame == 0, "first frame index");
  check(frames[0].first_canonical_sample == 0, "first frame origin");
  check(frames[1].time_frame == 1, "second frame index");
  check(frames[1].first_canonical_sample == 128, "second frame origin");

  constexpr double q31_scale = 2147483648.0;
  for (const std::size_t frame_index : std::array<std::size_t, 2>{0, 1}) {
    const std::size_t source_start = frame_index * FrameStream::kHopSize;
    for (const std::size_t sample_index : std::array<std::size_t, 5>{0, 1, 127, 255, 511}) {
      const float expected = static_cast<float>(
          static_cast<double>(samples[source_start + sample_index]) *
          (static_cast<double>(local_acr::kHannQ31[sample_index]) / q31_scale));
      check_equal(frames[frame_index].samples[sample_index], expected,
                  "windowed sample matches Q1.31 coefficient");
    }
  }
}

void arbitrary_chunks_are_byte_identical_across_ring_wrap() {
  const std::vector<float> samples = make_canonical_samples(4097);
  const std::vector<AnalysisFrame> expected = run_one_batch(samples);
  check(expected.size() == 29, "4097 samples emit 29 complete frames");

  std::uint32_t random = 0x48414E4EU;
  for (std::size_t trial = 0; trial < 32; ++trial) {
    std::vector<std::size_t> chunks;
    std::size_t remaining = samples.size();
    while (remaining > 0) {
      random = random * 1664525U + 1013904223U;
      const std::size_t proposed = static_cast<std::size_t>(random % 211U) + 1U;
      const std::size_t chunk = std::min(proposed, remaining);
      chunks.push_back(chunk);
      remaining -= chunk;
    }
    const std::vector<AnalysisFrame> actual = run_partitioned(samples, chunks);
    check(actual.size() == expected.size(), "partitioned frame count");
    if (actual.size() != expected.size()) {
      continue;
    }
    for (std::size_t frame = 0; frame < expected.size(); ++frame) {
      check(actual[frame].time_frame == expected[frame].time_frame, "partitioned frame index");
      check(actual[frame].first_canonical_sample == expected[frame].first_canonical_sample,
            "partitioned frame origin");
      for (std::size_t sample = 0; sample < FrameStream::kWindowSize; ++sample) {
        check_equal(actual[frame].samples[sample], expected[frame].samples[sample],
                    "partitioned frame bytes");
      }
    }
  }
}

void reset_starts_a_new_canonical_timeline() {
  const std::vector<float> samples = make_canonical_samples(512);
  FrameStream stream;
  CapturingFrameSink sink;

  check(stream.push(samples, sink).ok(), "first frame before reset");
  check(stream.finish().ok(), "first finish");
  check(stream.push(samples, sink).code() == StatusCode::InvalidState,
        "push after finish is invalid");
  stream.reset();
  check(stream.push(samples, sink).ok(), "frame after reset");
  check(sink.frames.size() == 2, "reset emits a second frame");
  if (sink.frames.size() == 2) {
    check(sink.frames[1].time_frame == 0, "reset frame index restarts");
    check(sink.frames[1].first_canonical_sample == 0, "reset origin restarts");
  }
}

}  // namespace

int main() {
  hann_table_is_checked_and_symmetric();
  incomplete_windows_are_not_emitted();
  complete_windows_start_at_exact_hops();
  arbitrary_chunks_are_byte_identical_across_ring_wrap();
  reset_starts_a_new_canonical_timeline();
  return failures == 0 ? 0 : 1;
}
