#include "audio/pcm_view.hpp"
#include "fingerprint/fingerprinter.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

namespace {

using local_acr::Fingerprinter;
using local_acr::Landmark;
using local_acr::LandmarkSink;
using local_acr::PcmPlane;
using local_acr::PcmView;
using local_acr::SampleFormat;
using local_acr::Status;

int failures = 0;

void check(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

void check_status_ok(Status status, std::string_view message) {
  if (!status.ok()) {
    std::cerr << "FAIL: " << message << " status=" << static_cast<int>(status.code()) << '\n';
    ++failures;
  }
}

class CapturingLandmarkSink final : public LandmarkSink {
 public:
  Status consume(std::span<const Landmark> batch) noexcept override {
    landmarks.insert(landmarks.end(), batch.begin(), batch.end());
    return Status::ok_status();
  }

  std::vector<Landmark> landmarks;
};

PcmPlane plane(std::span<const float> samples) {
  return PcmPlane{samples.data(), samples.size_bytes()};
}

std::vector<float> make_pcm(std::size_t count) {
  std::vector<float> samples(count);
  constexpr double two_pi = 6.283185307179586476925286766559;
  for (std::size_t index = 0; index < count; ++index) {
    const double phase = two_pi * static_cast<double>(index) / 64.0;
    const double gate = (index / 2048U) % 2U == 0U ? 0.9 : 0.2;
    samples[index] = static_cast<float>(gate * std::sin(phase));
  }
  return samples;
}

std::vector<Landmark> run_chunks(std::span<const float> samples,
                                 std::span<const std::size_t> chunks) {
  Fingerprinter fingerprinter(11025);
  CapturingLandmarkSink sink;
  std::uint64_t source_frame = 0;
  std::size_t offset = 0;
  for (const std::size_t chunk : chunks) {
    if (chunk == 0 || offset + chunk > samples.size()) {
      check(false, "chunk is in range");
      return {};
    }
    const std::span<const float> part = samples.subspan(offset, chunk);
    const std::array planes{plane(part)};
    const PcmView view{planes, static_cast<std::uint32_t>(chunk), 1, 11025, source_frame,
                       SampleFormat::F32Interleaved};
    check_status_ok(fingerprinter.push(view, sink), "fingerprinter chunk succeeds");
    source_frame += chunk;
    offset += chunk;
  }
  check(offset == samples.size(), "chunks cover PCM");
  check_status_ok(fingerprinter.finish_finite(sink), "fingerprinter finite finish succeeds");
  return sink.landmarks;
}

void identical_pcm_chunkings_are_byte_identical() {
  const std::vector<float> samples = make_pcm(44100);
  std::vector<std::size_t> expected_chunks;
  std::size_t expected_remaining = samples.size();
  while (expected_remaining > 0) {
    const std::size_t chunk =
        std::min<std::size_t>(Fingerprinter::kMaxPcmFramesPerPush, expected_remaining);
    expected_chunks.push_back(chunk);
    expected_remaining -= chunk;
  }
  const std::vector<Landmark> expected = run_chunks(samples, expected_chunks);
  check(!expected.empty(), "synthetic PCM emits at least one landmark");

  std::vector<std::size_t> chunks;
  std::size_t remaining = samples.size();
  std::uint32_t random = 0x4C414352U;
  while (remaining > 0) {
    random = random * 1664525U + 1013904223U;
    const std::size_t chunk = std::min<std::size_t>((random % 700U) + 1U, remaining);
    chunks.push_back(chunk);
    remaining -= chunk;
  }

  const std::vector<Landmark> actual = run_chunks(samples, chunks);
  check(actual == expected, "different PCM chunking emits identical landmarks");
}

void reset_restarts_the_fingerprint_timeline() {
  const std::vector<float> samples = make_pcm(Fingerprinter::kMaxPcmFramesPerPush);
  Fingerprinter fingerprinter(11025);
  CapturingLandmarkSink sink;
  const std::array planes{plane(std::span<const float>{samples})};
  const PcmView view{planes, static_cast<std::uint32_t>(samples.size()), 1, 11025, 0,
                     SampleFormat::F32Interleaved};

  check_status_ok(fingerprinter.push(view, sink), "first run succeeds");
  check_status_ok(fingerprinter.finish_finite(sink), "first finish succeeds");
  const std::size_t first_count = sink.landmarks.size();

  fingerprinter.reset();
  check_status_ok(fingerprinter.push(view, sink), "second run after reset succeeds");
  check_status_ok(fingerprinter.finish_finite(sink), "second finish succeeds");
  check(sink.landmarks.size() == first_count * 2U, "reset permits repeat processing");
}

}  // namespace

int main() {
  identical_pcm_chunkings_are_byte_identical();
  reset_restarts_the_fingerprint_timeline();
  return failures == 0 ? 0 : 1;
}
