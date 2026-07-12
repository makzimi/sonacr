#include "audio/pcm_view.hpp"
#include "fingerprint/fingerprinter.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

class CapturingLandmarkSink final : public local_acr::LandmarkSink {
 public:
  local_acr::Status consume(std::span<const local_acr::Landmark> batch) noexcept override {
    landmarks.insert(landmarks.end(), batch.begin(), batch.end());
    return local_acr::Status::ok_status();
  }

  std::vector<local_acr::Landmark> landmarks;
};

std::vector<std::int16_t> make_quantized_fixture() {
  std::vector<std::int16_t> samples(11025 * 6);
  constexpr double two_pi = 6.283185307179586476925286766559;
  for (std::size_t index = 0; index < samples.size(); ++index) {
    const double fast = std::sin(two_pi * static_cast<double>(index) / 67.0);
    const double slow = std::sin(two_pi * static_cast<double>(index) / 191.0);
    const double gate = ((index / 1536U) % 3U == 0U) ? 0.92 : 0.34;
    const double value = (gate * fast) + (0.21 * slow);
    const auto quantized = static_cast<int>(std::lrint(std::max(-0.95, std::min(0.95, value)) * 32767.0));
    samples[index] = static_cast<std::int16_t>(quantized);
  }
  return samples;
}

std::vector<float> to_float(std::span<const std::int16_t> samples) {
  std::vector<float> out;
  out.reserve(samples.size());
  for (const std::int16_t sample : samples) {
    out.push_back(static_cast<float>(sample) / 32768.0F);
  }
  return out;
}

local_acr::PcmPlane plane(std::span<const float> samples) {
  return local_acr::PcmPlane{samples.data(), samples.size_bytes()};
}

local_acr::PcmPlane plane_s16(std::span<const std::int16_t> samples) {
  return local_acr::PcmPlane{samples.data(), samples.size_bytes()};
}

std::vector<local_acr::Landmark> landmarks_from_float(std::span<const float> samples,
                                                      std::span<const std::size_t> chunks) {
  local_acr::Fingerprinter fingerprinter(11025);
  CapturingLandmarkSink sink;
  std::uint64_t source_frame = 0;
  std::size_t offset = 0;
  std::size_t chunk_index = 0;
  while (offset < samples.size()) {
    const std::size_t requested = chunks.empty() ? samples.size() : chunks[chunk_index++ % chunks.size()];
    const std::size_t count = std::min(requested, samples.size() - offset);
    const std::span<const float> part = samples.subspan(offset, count);
    const std::array planes{plane(part)};
    const local_acr::PcmView view{
        .planes = planes,
        .frames = static_cast<std::uint32_t>(count),
        .channels = 1,
        .sample_rate = 11025,
        .first_source_frame = source_frame,
        .format = local_acr::SampleFormat::F32Interleaved,
    };
    check(fingerprinter.push(view, sink).ok(), "float push succeeds");
    source_frame += count;
    offset += count;
  }
  check(fingerprinter.finish_finite(sink).ok(), "float finish succeeds");
  return sink.landmarks;
}

std::vector<local_acr::Landmark> landmarks_from_s16(std::span<const std::int16_t> samples,
                                                    std::span<const std::size_t> chunks) {
  local_acr::Fingerprinter fingerprinter(11025);
  CapturingLandmarkSink sink;
  std::uint64_t source_frame = 0;
  std::size_t offset = 0;
  std::size_t chunk_index = 0;
  while (offset < samples.size()) {
    const std::size_t requested = chunks[chunk_index++ % chunks.size()];
    const std::size_t count = std::min(requested, samples.size() - offset);
    const std::span<const std::int16_t> part = samples.subspan(offset, count);
    const std::array planes{plane_s16(part)};
    const local_acr::PcmView view{
        .planes = planes,
        .frames = static_cast<std::uint32_t>(count),
        .channels = 1,
        .sample_rate = 11025,
        .first_source_frame = source_frame,
        .format = local_acr::SampleFormat::S16Interleaved,
    };
    check(fingerprinter.push(view, sink).ok(), "s16 push succeeds");
    source_frame += count;
    offset += count;
  }
  check(fingerprinter.finish_finite(sink).ok(), "s16 finish succeeds");
  return sink.landmarks;
}

std::string serialize(std::span<const local_acr::Landmark> landmarks) {
  std::ostringstream out;
  out << "{\n  \"profile\": \"landmark-v1\",\n  \"vectors\": [\n";
  for (std::size_t i = 0; i < landmarks.size(); ++i) {
    out << "    {\"hash\":" << landmarks[i].hash
        << ",\"timeFrame\":" << landmarks[i].anchor_time_frame << "}";
    if (i + 1U != landmarks.size()) {
      out << ',';
    }
    out << '\n';
  }
  out << "  ]\n}\n";
  return out.str();
}

std::filesystem::path golden_path() {
  return std::filesystem::path(LACR_SOURCE_DIR) / "native/tests/golden/parity_landmarks.json";
}

}  // namespace

int main(int argc, char** argv) {
  const std::vector<std::int16_t> s16 = make_quantized_fixture();
  const std::vector<float> f32 = to_float(s16);
  const std::array<std::size_t, 1> one_chunk{local_acr::Fingerprinter::kMaxPcmFramesPerPush};
  const std::array<std::size_t, 7> uneven_chunks{1, 257, 1024, 333, 4096, 17, 819};

  const std::vector<local_acr::Landmark> baseline = landmarks_from_float(f32, one_chunk);
  const std::vector<local_acr::Landmark> chunked = landmarks_from_float(f32, uneven_chunks);
  const std::vector<local_acr::Landmark> integer_path = landmarks_from_s16(s16, uneven_chunks);

  check(baseline.size() >= 12U, "fixture produces at least 12 landmarks");
  check(baseline == chunked, "uneven Float32 chunks match one-batch golden");
  check(baseline == integer_path, "S16 path matches Float32 path");

  const std::string serialized = serialize(baseline);
  if (argc == 3 && std::string_view(argv[1]) == "--write-golden") {
    std::ofstream out(argv[2], std::ios::binary | std::ios::trunc);
    out << serialized;
    return failures == 0 ? 0 : 1;
  }

  std::ifstream input(golden_path(), std::ios::binary);
  check(input.good(), "parity golden file exists");
  std::ostringstream expected;
  expected << input.rdbuf();
  check(serialized == expected.str(), "serialized landmarks match parity golden");
  return failures == 0 ? 0 : 1;
}
