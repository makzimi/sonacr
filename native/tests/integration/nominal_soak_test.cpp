#include "audio/pcm_view.hpp"
#include "database/database_reader.hpp"
#include "database/database_writer.hpp"
#include "fingerprint/fingerprinter.hpp"
#include "session/recognizer.hpp"
#include "session/spsc_pcm_queue.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <unistd.h>

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

std::filesystem::path temp_path(std::string_view name) {
  return std::filesystem::temp_directory_path() / "local-acr" /
         ("soak-" + std::to_string(static_cast<long long>(::getpid()))) / std::string(name);
}

std::vector<float> make_pcm(std::size_t count) {
  std::vector<float> samples(count);
  constexpr double two_pi = 6.283185307179586476925286766559;
  for (std::size_t index = 0; index < samples.size(); ++index) {
    const double fast = std::sin(two_pi * static_cast<double>(index) / 73.0);
    const double slow = std::sin(two_pi * static_cast<double>(index) / 211.0);
    samples[index] = static_cast<float>((0.76 * fast) + (0.18 * slow));
  }
  return samples;
}

local_acr::PcmPlane plane(std::span<const float> samples) {
  return local_acr::PcmPlane{samples.data(), samples.size_bytes()};
}

local_acr::PcmView view(std::span<const local_acr::PcmPlane> planes,
                        std::uint32_t frames,
                        std::uint64_t first_source_frame) {
  return local_acr::PcmView{
      .planes = planes,
      .frames = frames,
      .channels = 1,
      .sample_rate = 11025,
      .first_source_frame = first_source_frame,
      .format = local_acr::SampleFormat::F32Interleaved,
  };
}

std::filesystem::path write_database(std::span<const float> samples) {
  local_acr::Fingerprinter fingerprinter(11025);
  CapturingLandmarkSink sink;
  std::uint64_t source_frame = 0;
  std::size_t offset = 0;
  while (offset < samples.size()) {
    const std::size_t count =
        std::min<std::size_t>(local_acr::Fingerprinter::kMaxPcmFramesPerPush, samples.size() - offset);
    const std::span<const float> part = samples.subspan(offset, count);
    const std::array planes{plane(part)};
    check(fingerprinter.push(view(planes, static_cast<std::uint32_t>(count), source_frame), sink).ok(),
          "database fixture push");
    source_frame += count;
    offset += count;
  }
  check(fingerprinter.finish_finite(sink).ok(), "database fixture finish");

  std::vector<local_acr::FingerprintRow> rows;
  for (const local_acr::Landmark& landmark : sink.landmarks) {
    rows.push_back(local_acr::FingerprintRow{.trigger_id = "cue", .landmark = landmark});
  }
  check(rows.size() >= 12U, "database fixture has enough landmarks");

  const local_acr::SemanticDatabase database{
      .metadata =
          local_acr::DatabaseMetadata{
              .database_id = "soak",
              .database_version = "1",
              .build_report_json = "{}",
              .decoder_version = "test",
          },
      .triggers =
          {
              local_acr::DatabaseTrigger{
                  .trigger_id = "cue",
                  .display_name = "Cue",
                  .duration_ms = 6000,
                  .metadata_json = "{}",
              },
          },
      .fingerprints = std::move(rows),
  };

  std::filesystem::create_directories(temp_path("").parent_path());
  const std::filesystem::path path = temp_path("soak.lacrdb");
  std::filesystem::remove(path);
  check(local_acr::DatabaseWriter::write(path, database).ok(), "soak database writes");
  return path;
}

void queue_soak_preserves_order_for_30_minutes_of_chunks() {
  local_acr::SpscPcmQueue queue(8);
  constexpr std::uint32_t sample_rate = 11025;
  constexpr std::uint32_t frames_per_20ms = sample_rate / 50;
  constexpr std::uint32_t chunks = 30U * 60U * 50U;
  std::uint32_t popped = 0;
  for (std::uint32_t chunk = 0; chunk < chunks; ++chunk) {
    local_acr::QueuedPcmChunk queued;
    queued.generation = 11;
    queued.bytes.resize(frames_per_20ms * sizeof(float));
    queued.bytes[0] = static_cast<std::uint8_t>(chunk & 0xFFU);
    check(queue.try_push(std::move(queued)).ok(), "queue push succeeds under nominal drain");
    const std::optional<local_acr::QueuedPcmChunk> out = queue.try_pop();
    check(out.has_value(), "queue pop succeeds under nominal drain");
    if (out.has_value()) {
      check(out->generation == 11, "queue generation preserved");
      check(out->bytes[0] == static_cast<std::uint8_t>(chunk & 0xFFU), "queue order preserved");
      ++popped;
    }
  }
  check(popped == chunks, "all nominal chunks popped");
  check(queue.size() == 0U, "queue empty after nominal soak");
}

void recognizer_soak_emits_no_session_errors() {
  const std::vector<float> cue = make_pcm(11025 * 6);
  const std::filesystem::path path = write_database(cue);
  local_acr::DatabaseReader reader;
  check(reader.open(path).ok(), "soak database opens");
  local_acr::Recognizer recognizer(11025);
  check(recognizer.prepare(reader).ok(), "soak recognizer prepares");
  check(recognizer.start_session(99).ok(), "soak recognizer starts");

  std::uint64_t source_frame = 0;
  for (std::size_t repeat = 0; repeat < 8; ++repeat) {
    std::size_t offset = 0;
    while (offset < cue.size()) {
      const std::size_t count = std::min<std::size_t>(441, cue.size() - offset);
      const std::span<const float> part = std::span<const float>(cue).subspan(offset, count);
      const std::array planes{plane(part)};
      check(recognizer.push_pcm(view(planes, static_cast<std::uint32_t>(count), source_frame)).ok(),
            "soak recognizer push succeeds");
      source_frame += count;
      offset += count;
    }
  }

  while (true) {
    const std::optional<local_acr::RecognizerEvent> event = recognizer.poll_event();
    if (!event.has_value()) {
      break;
    }
    check(event->type != local_acr::RecognizerEventType::SessionError, "nominal input emits no session error");
  }
}

}  // namespace

int main() {
  queue_soak_preserves_order_for_30_minutes_of_chunks();
  recognizer_soak_emits_no_session_errors();
  return failures == 0 ? 0 : 1;
}
