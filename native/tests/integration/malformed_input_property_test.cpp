#include "local_acr/local_acr.h"

#include "audio/pcm_view.hpp"
#include "database/database_writer.hpp"
#include "fingerprint/fingerprinter.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
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
         ("malformed-" + std::to_string(static_cast<long long>(::getpid()))) / std::string(name);
}

std::vector<float> make_pcm() {
  std::vector<float> samples(11025 * 5);
  constexpr double two_pi = 6.283185307179586476925286766559;
  for (std::size_t index = 0; index < samples.size(); ++index) {
    samples[index] = static_cast<float>(0.72 * std::sin(two_pi * static_cast<double>(index) / 83.0));
  }
  return samples;
}

local_acr::PcmPlane plane(std::span<const float> samples) {
  return local_acr::PcmPlane{samples.data(), samples.size_bytes()};
}

std::filesystem::path write_database() {
  const std::vector<float> samples = make_pcm();
  local_acr::Fingerprinter fingerprinter(11025);
  CapturingLandmarkSink sink;
  std::uint64_t source_frame = 0;
  std::size_t offset = 0;
  while (offset < samples.size()) {
    const std::size_t count =
        std::min<std::size_t>(local_acr::Fingerprinter::kMaxPcmFramesPerPush, samples.size() - offset);
    const std::span<const float> part = std::span<const float>(samples).subspan(offset, count);
    const std::array planes{plane(part)};
    const local_acr::PcmView view{
        .planes = planes,
        .frames = static_cast<std::uint32_t>(count),
        .channels = 1,
        .sample_rate = 11025,
        .first_source_frame = source_frame,
        .format = local_acr::SampleFormat::F32Interleaved,
    };
    check(fingerprinter.push(view, sink).ok(), "fingerprinter push");
    source_frame += count;
    offset += count;
  }
  check(fingerprinter.finish_finite(sink).ok(), "fingerprinter finish");

  std::vector<local_acr::FingerprintRow> rows;
  for (const local_acr::Landmark& landmark : sink.landmarks) {
    rows.push_back(local_acr::FingerprintRow{.trigger_id = "cue", .landmark = landmark});
  }
  check(rows.size() >= 12U, "fixture has enough rows");

  const local_acr::SemanticDatabase database{
      .metadata =
          local_acr::DatabaseMetadata{
              .database_id = "malformed",
              .database_version = "1",
              .build_report_json = "{}",
              .decoder_version = "test",
          },
      .triggers =
          {
              local_acr::DatabaseTrigger{
                  .trigger_id = "cue",
                  .display_name = "Cue",
                  .duration_ms = 5000,
                  .metadata_json = "{}",
              },
          },
      .fingerprints = std::move(rows),
  };

  std::filesystem::create_directories(temp_path("").parent_path());
  const std::filesystem::path path = temp_path("valid.lacrdb");
  std::filesystem::remove(path);
  check(local_acr::DatabaseWriter::write(path, database).ok(), "database writes");
  return path;
}

lacr_recognizer_t* create_prepared_active(const std::filesystem::path& path, std::uint64_t generation) {
  lacr_recognizer_t* recognizer = nullptr;
  lacr_config_t config{.abi_version = LACR_ABI_VERSION, .input_sample_rate = 11025};
  check(lacr_recognizer_create(path.string().c_str(), &config, &recognizer, nullptr) == LACR_STATUS_OK,
        "create valid recognizer");
  check(lacr_recognizer_prepare(recognizer, nullptr) == LACR_STATUS_OK, "prepare valid recognizer");
  check(lacr_recognizer_start_session(recognizer, generation, nullptr) == LACR_STATUS_OK,
        "start valid recognizer");
  return recognizer;
}

void creation_validation() {
  lacr_recognizer_t* recognizer = nullptr;
  lacr_config_t valid{.abi_version = LACR_ABI_VERSION, .input_sample_rate = 11025};
  lacr_config_t wrong_abi{.abi_version = LACR_ABI_VERSION + 1U, .input_sample_rate = 11025};
  lacr_config_t zero_rate{.abi_version = LACR_ABI_VERSION, .input_sample_rate = 0};
  const std::string too_long(4097, 'a');

  check(lacr_recognizer_create(nullptr, &valid, &recognizer, nullptr) == LACR_STATUS_INVALID_ARGUMENT,
        "null path rejected");
  check(lacr_recognizer_create("", &valid, &recognizer, nullptr) == LACR_STATUS_INVALID_ARGUMENT,
        "empty path rejected");
  check(lacr_recognizer_create(too_long.c_str(), &valid, &recognizer, nullptr) ==
            LACR_STATUS_INVALID_ARGUMENT,
        "4097-byte path rejected");
  check(lacr_recognizer_create("/tmp/missing.lacrdb", nullptr, &recognizer, nullptr) ==
            LACR_STATUS_INVALID_ARGUMENT,
        "null config rejected");
  check(lacr_recognizer_create("/tmp/missing.lacrdb", &valid, nullptr, nullptr) ==
            LACR_STATUS_INVALID_ARGUMENT,
        "null out rejected");
  check(lacr_recognizer_create("/tmp/missing.lacrdb", &wrong_abi, &recognizer, nullptr) ==
            LACR_STATUS_INVALID_ARGUMENT,
        "wrong ABI rejected");
  check(lacr_recognizer_create("/tmp/missing.lacrdb", &zero_rate, &recognizer, nullptr) ==
            LACR_STATUS_INVALID_ARGUMENT,
        "zero sample rate rejected");
}

void pcm_validation(const std::filesystem::path& path) {
  std::array<float, 128> samples{};
  const void* valid_plane = samples.data();
  const void* null_plane = nullptr;
  const std::array<const void*, 1> valid_planes{valid_plane};
  const std::array<const void*, 1> null_planes{null_plane};
  lacr_recognizer_t* recognizer = create_prepared_active(path, 7);

  lacr_pcm_view_t valid{
      .planes = valid_planes.data(),
      .plane_count = 1,
      .frames = static_cast<std::uint32_t>(samples.size()),
      .channels = 1,
      .sample_rate = 11025,
      .first_source_frame = 0,
      .format = LACR_F32_INTERLEAVED,
  };

  lacr_pcm_view_t test = valid;
  test.planes = nullptr;
  check(lacr_recognizer_push_pcm(recognizer, 7, &test) == LACR_STATUS_INVALID_ARGUMENT,
        "null plane array rejected");
  test = valid;
  test.plane_count = 2;
  check(lacr_recognizer_push_pcm(recognizer, 7, &test) == LACR_STATUS_INVALID_ARGUMENT,
        "interleaved wrong plane count rejected");
  test = valid;
  test.format = LACR_F32_PLANAR;
  test.plane_count = 1;
  test.channels = 2;
  check(lacr_recognizer_push_pcm(recognizer, 7, &test) == LACR_STATUS_INVALID_ARGUMENT,
        "planar wrong plane count rejected");
  test = valid;
  test.planes = null_planes.data();
  check(lacr_recognizer_push_pcm(recognizer, 7, &test) == LACR_STATUS_INVALID_ARGUMENT,
        "null plane rejected");
  test = valid;
  test.frames = 0;
  check(lacr_recognizer_push_pcm(recognizer, 7, &test) == LACR_STATUS_INVALID_ARGUMENT,
        "zero frames rejected");
  test = valid;
  test.channels = 0;
  check(lacr_recognizer_push_pcm(recognizer, 7, &test) == LACR_STATUS_INVALID_ARGUMENT,
        "zero channels rejected");
  test = valid;
  test.format = static_cast<lacr_sample_format_t>(99);
  check(lacr_recognizer_push_pcm(recognizer, 7, &test) == LACR_STATUS_INVALID_ARGUMENT,
        "invalid sample format rejected");

  check(lacr_recognizer_stop_session(recognizer, 7, nullptr) == LACR_STATUS_OK, "stop active session");
  check(lacr_recognizer_push_pcm(recognizer, 7, &valid) == LACR_STATUS_INVALID_STATE,
        "stale generation push rejected after stop");
  lacr_recognizer_destroy(recognizer);
}

}  // namespace

int main() {
  creation_validation();
  const std::filesystem::path path = write_database();
  pcm_validation(path);
  return failures == 0 ? 0 : 1;
}
