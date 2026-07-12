#include "audio/pcm_view.hpp"
#include "database/database_reader.hpp"
#include "database/database_writer.hpp"
#include "fingerprint/fingerprinter.hpp"
#include "session/recognizer.hpp"

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

void check_status_ok(local_acr::Status status, std::string_view message) {
  if (!status.ok()) {
    std::cerr << "FAIL: " << message << " status=" << static_cast<int>(status.code()) << '\n';
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

local_acr::PcmPlane plane(std::span<const float> samples) {
  return local_acr::PcmPlane{samples.data(), samples.size_bytes()};
}

local_acr::PcmView view_with_plane(std::span<const local_acr::PcmPlane> planes,
                                   std::uint32_t frames,
                                   std::uint64_t source_frame) {
  return local_acr::PcmView{
      .planes = planes,
      .frames = frames,
      .channels = 1,
      .sample_rate = 11025,
      .first_source_frame = source_frame,
      .format = local_acr::SampleFormat::F32Interleaved,
  };
}

std::vector<float> make_pcm(std::size_t count) {
  std::vector<float> samples(count);
  constexpr double two_pi = 6.283185307179586476925286766559;
  for (std::size_t index = 0; index < count; ++index) {
    const double phase_a = two_pi * static_cast<double>(index) / 64.0;
    const double phase_b = two_pi * static_cast<double>(index) / 97.0;
    const double gate = (index / 2048U) % 2U == 0U ? 0.95 : 0.25;
    samples[index] = static_cast<float>((gate * std::sin(phase_a)) + (0.35 * std::sin(phase_b)));
  }
  return samples;
}

std::vector<local_acr::Landmark> landmarks_for(std::span<const float> samples) {
  local_acr::Fingerprinter fingerprinter(11025);
  CapturingLandmarkSink sink;
  std::uint64_t source_frame = 0;
  std::size_t offset = 0;
  while (offset < samples.size()) {
    const std::size_t chunk =
        std::min<std::size_t>(local_acr::Fingerprinter::kMaxPcmFramesPerPush, samples.size() - offset);
    const std::span<const float> part = samples.subspan(offset, chunk);
    const std::array planes{plane(part)};
    check_status_ok(fingerprinter.push(view_with_plane(planes, static_cast<std::uint32_t>(chunk),
                                                       source_frame),
                                       sink),
                    "fingerprinter push succeeds");
    source_frame += chunk;
    offset += chunk;
  }
  check_status_ok(fingerprinter.finish_finite(sink), "fingerprinter finish succeeds");
  return sink.landmarks;
}

std::filesystem::path temp_path(std::string_view name) {
  return std::filesystem::temp_directory_path() / "local-acr" /
         ("recognizer-" + std::to_string(static_cast<long long>(::getpid()))) / std::string(name);
}

local_acr::DatabaseReader build_reader_from_pcm(std::span<const float> samples) {
  std::vector<local_acr::FingerprintRow> rows;
  for (const local_acr::Landmark& landmark : landmarks_for(samples)) {
    rows.push_back(local_acr::FingerprintRow{.trigger_id = "cue", .landmark = landmark});
  }
  check(rows.size() >= 12U, "fixture emits enough fingerprints");

  const local_acr::SemanticDatabase database{
      .metadata =
          local_acr::DatabaseMetadata{
              .database_id = "recognizer",
              .database_version = "1",
              .build_report_json = "{}",
              .decoder_version = "test",
          },
      .triggers =
          {
              local_acr::DatabaseTrigger{
                  .trigger_id = "cue",
                  .display_name = "Cue",
                  .duration_ms = 60000,
                  .metadata_json = "{}",
              },
          },
      .fingerprints = std::move(rows),
  };

  std::filesystem::create_directories(temp_path("").parent_path());
  const std::filesystem::path path = temp_path("recognizer.lacrdb");
  std::filesystem::remove(path);
  check_status_ok(local_acr::DatabaseWriter::write(path, database), "recognizer database writes");

  local_acr::DatabaseReader reader;
  check_status_ok(reader.open(path), "recognizer database opens");
  return reader;
}

void push_pcm(local_acr::Recognizer& recognizer, std::span<const float> samples) {
  std::uint64_t source_frame = 0;
  std::size_t offset = 0;
  while (offset < samples.size()) {
    const std::size_t chunk = std::min<std::size_t>(777, samples.size() - offset);
    const std::span<const float> part = samples.subspan(offset, chunk);
    const std::array planes{plane(part)};
    check_status_ok(recognizer.push_pcm(view_with_plane(planes, static_cast<std::uint32_t>(chunk),
                                                        source_frame)),
                    "recognizer push succeeds");
    source_frame += chunk;
    offset += chunk;
  }
}

std::vector<local_acr::RecognizerEvent> drain(local_acr::Recognizer& recognizer) {
  std::vector<local_acr::RecognizerEvent> events;
  while (true) {
    const std::optional<local_acr::RecognizerEvent> event = recognizer.poll_event();
    if (!event.has_value()) {
      break;
    }
    events.push_back(*event);
  }
  return events;
}

void recognizer_emits_generation_tagged_match_after_consecutive_evaluations() {
  const std::vector<float> samples = make_pcm(11025 * 6);
  local_acr::DatabaseReader reader = build_reader_from_pcm(samples);
  local_acr::Recognizer recognizer(11025);

  check_status_ok(recognizer.prepare(reader), "recognizer prepares");
  check_status_ok(recognizer.start_session(42), "recognizer starts");
  push_pcm(recognizer, samples);

  const std::vector<local_acr::RecognizerEvent> events = drain(recognizer);
  check(!events.empty(), "recognizer emits at least one event");
  bool found = false;
  for (const local_acr::RecognizerEvent& event : events) {
    if (event.type == local_acr::RecognizerEventType::Recognition) {
      found = true;
      check(event.generation == 42, "recognition carries generation");
      check(event.recognition.trigger_id == "cue", "recognition trigger id");
      check(event.recognition.newest_source_frame > 0, "recognition carries source frame");
      check(event.recognition.matched_cue_time_frame > 0, "recognition carries cue frame");
    }
  }
  check(found, "recognition event is emitted");
}

void stop_and_restart_resets_timeline_and_generation() {
  const std::vector<float> samples = make_pcm(11025 * 5);
  local_acr::DatabaseReader reader = build_reader_from_pcm(samples);
  local_acr::Recognizer recognizer(11025);
  check_status_ok(recognizer.prepare(reader), "recognizer prepares for reset");

  check_status_ok(recognizer.start_session(1), "first session starts");
  push_pcm(recognizer, samples);
  check_status_ok(recognizer.stop_session(), "first session stops");
  (void)drain(recognizer);

  check_status_ok(recognizer.start_session(2), "second session starts");
  push_pcm(recognizer, samples);
  const std::vector<local_acr::RecognizerEvent> events = drain(recognizer);
  bool found_generation_two = false;
  for (const local_acr::RecognizerEvent& event : events) {
    if (event.type == local_acr::RecognizerEventType::Recognition && event.generation == 2) {
      found_generation_two = true;
    }
    check(event.generation != 1, "old generation is not emitted after restart drain");
  }
  check(found_generation_two, "second session emits generation two recognition");
}

void query_density_error_is_terminal_and_resets_session() {
  const std::vector<float> samples = make_pcm(11025 * 3);
  local_acr::DatabaseReader reader = build_reader_from_pcm(samples);
  local_acr::Recognizer recognizer(11025);
  check_status_ok(recognizer.prepare(reader), "density recognizer prepares");
  check_status_ok(recognizer.start_session(9), "density session starts");
  check(recognizer.inject_query_landmarks_for_test(513, 200).code() ==
            local_acr::StatusCode::ResourceLimitExceeded,
        "density test returns resource-limit status");

  const std::vector<local_acr::RecognizerEvent> events = drain(recognizer);
  check(events.size() == 1, "density emits one terminal event");
  if (!events.empty()) {
    check(events[0].type == local_acr::RecognizerEventType::SessionError, "density emits session error");
    check(events[0].error == local_acr::RecognizerError::QueryDensityExceeded, "density error code");
    check(events[0].generation == 9, "density error carries generation");
  }
  check(!recognizer.active(), "density error resets the session");
}

}  // namespace

int main() {
  recognizer_emits_generation_tagged_match_after_consecutive_evaluations();
  stop_and_restart_resets_timeline_and_generation();
  query_density_error_is_terminal_and_resets_session();
  return failures == 0 ? 0 : 1;
}
