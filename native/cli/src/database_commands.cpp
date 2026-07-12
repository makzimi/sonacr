#include "database_commands.hpp"

#include "ambiguity_gate.hpp"
#include "audio_decoder.hpp"
#include "audio/pcm_view.hpp"
#include "database/database_reader.hpp"
#include "database/database_types.hpp"
#include "database/database_writer.hpp"
#include "fingerprint/fingerprinter.hpp"
#include "manifest.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace local_acr::cli {

namespace {

constexpr std::uint32_t kCanonicalSampleRate = 11025;
constexpr std::uint64_t kMaxAggregatePcmBytes = 60ULL * 60ULL * kCanonicalSampleRate * sizeof(float);
constexpr std::string_view kBuilderProfile = "local-acr-db-mvp";

struct CapturingLandmarkSink final : public LandmarkSink {
  Status consume(std::span<const Landmark> batch) noexcept override {
    landmarks.insert(landmarks.end(), batch.begin(), batch.end());
    return Status::ok_status();
  }

  std::vector<Landmark> landmarks;
};

class FfmpegAudioDecoder final : public AudioDecoder {
 public:
  DecodeResult decode(const std::filesystem::path& path, std::uint64_t max_pcm_bytes) override {
    const FfmpegDecoderPlan plan = make_ffmpeg_decoder_plan("ffprobe", "ffmpeg", path);
    ProcessResult process = run_process_capture_stdout(plan.ffmpeg_argv, static_cast<std::size_t>(max_pcm_bytes));
    if (!process.started || process.exit_code != 0 || process.stdout_limit_exceeded ||
        process.stdout_bytes.size() % sizeof(float) != 0U) {
      return DecodeResult{.status = Status::invalid_argument()};
    }
    std::vector<float> samples(process.stdout_bytes.size() / sizeof(float));
    if (!samples.empty()) {
      std::memcpy(samples.data(), process.stdout_bytes.data(), process.stdout_bytes.size());
    }
    return DecodeResult{
        .status = Status::ok_status(),
        .audio =
            DecodedAudio{
                .samples = std::move(samples),
                .sample_rate = kCanonicalSampleRate,
                .decoder_version = "ffmpeg:host-profile-v1",
            },
    };
  }
};

CommandResult result(ExitCode exit_code, std::string stdout_text = {}, std::string stderr_text = {}) {
  return CommandResult{
      .exit_code = exit_code,
      .stdout_text = std::move(stdout_text),
      .stderr_text = std::move(stderr_text),
  };
}

std::string json_identity(const DatabaseIdentity& identity) {
  return "{\"databaseId\":\"" + identity.database_id + "\",\"databaseVersion\":\"" + identity.database_version +
         "\",\"fingerprintProfile\":\"" + identity.fingerprint_profile + "\",\"matcherProfile\":\"" +
         identity.matcher_profile + "\",\"triggers\":" + std::to_string(identity.trigger_count) +
         ",\"fingerprints\":" + std::to_string(identity.fingerprint_count) + ",\"contentDigest\":\"" +
         identity.content_digest_sha256 + "\"}\n";
}

PcmPlane plane(std::span<const float> samples) {
  return PcmPlane{samples.data(), samples.size_bytes()};
}

PcmView pcm_view(std::span<const PcmPlane> planes, std::uint32_t frames, std::uint64_t first_source_frame) {
  return PcmView{
      .planes = planes,
      .frames = frames,
      .channels = 1,
      .sample_rate = kCanonicalSampleRate,
      .first_source_frame = first_source_frame,
      .format = SampleFormat::F32Interleaved,
  };
}

Status validate_audio(const DecodedAudio& audio) {
  if (audio.sample_rate != kCanonicalSampleRate || audio.samples.empty()) {
    return Status::invalid_argument();
  }
  double abs_sum = 0.0;
  for (const float sample : audio.samples) {
    if (!std::isfinite(sample) || std::fabs(sample) > 2.0F) {
      return Status::invalid_argument();
    }
    abs_sum += std::fabs(static_cast<double>(sample));
  }
  if (abs_sum / static_cast<double>(audio.samples.size()) < 0.000001) {
    return Status::invalid_argument();
  }
  return Status::ok_status();
}

Status fingerprint_audio(std::string_view trigger_id,
                         const DecodedAudio& audio,
                         std::vector<FingerprintRow>& out) {
  Fingerprinter fingerprinter(audio.sample_rate);
  if (!fingerprinter.valid()) {
    return Status::native_engine_failure();
  }

  CapturingLandmarkSink sink;
  std::uint64_t source_frame = 0;
  std::size_t offset = 0;
  while (offset < audio.samples.size()) {
    const std::size_t chunk =
        std::min<std::size_t>(Fingerprinter::kMaxPcmFramesPerPush, audio.samples.size() - offset);
    const std::span<const float> part = std::span<const float>(audio.samples).subspan(offset, chunk);
    const std::array planes{plane(part)};
    const Status status = fingerprinter.push(pcm_view(planes, static_cast<std::uint32_t>(chunk), source_frame), sink);
    if (!status.ok()) {
      return status;
    }
    source_frame += chunk;
    offset += chunk;
  }
  const Status finish_status = fingerprinter.finish_finite(sink);
  if (!finish_status.ok()) {
    return finish_status;
  }
  if (sink.landmarks.empty()) {
    return Status::invalid_argument();
  }
  out.reserve(out.size() + sink.landmarks.size());
  for (const Landmark& landmark : sink.landmarks) {
    out.push_back(FingerprintRow{.trigger_id = std::string(trigger_id), .landmark = landmark});
  }
  return Status::ok_status();
}

std::filesystem::path temp_output_path(const std::filesystem::path& output) {
  const std::filesystem::path parent = output.parent_path();
  const std::filesystem::path temp_name = output.filename().string() + ".tmp";
  return parent.empty() ? temp_name : parent / temp_name;
}

CommandResult build_command(const std::filesystem::path& manifest_path,
                            const std::filesystem::path& output_path,
                            AudioDecoder& decoder) {
  ManifestResult manifest_result = load_manifest(manifest_path);
  if (!manifest_result.status.ok()) {
    return result(ExitCode::Validation, {}, "invalid manifest\n");
  }

  SemanticDatabase database;
  database.metadata.database_id = manifest_result.manifest.database_id;
  database.metadata.database_version = manifest_result.manifest.database_version;
  database.metadata.build_report_json = "{\"builder\":\"" + std::string(kBuilderProfile) + "\"}";

  std::uint64_t aggregate_pcm_bytes = 0;
  std::string decoder_version;
  for (const ManifestTrigger& trigger : manifest_result.manifest.triggers) {
    DecodeResult decoded = decoder.decode(trigger.audio_path, kMaxAggregatePcmBytes - aggregate_pcm_bytes);
    if (!decoded.status.ok()) {
      return result(ExitCode::DecodeFailure, {}, "audio decode failed\n");
    }
    const std::uint64_t pcm_bytes = decoded.audio.samples.size() * sizeof(float);
    if (pcm_bytes == 0U || pcm_bytes > kMaxAggregatePcmBytes - aggregate_pcm_bytes) {
      return result(ExitCode::Validation, {}, "audio duration limit exceeded\n");
    }
    aggregate_pcm_bytes += pcm_bytes;
    decoder_version = decoded.audio.decoder_version;

    const Status audio_status = validate_audio(decoded.audio);
    if (!audio_status.ok()) {
      return result(ExitCode::Validation, {}, "invalid decoded audio\n");
    }
    const Status fingerprint_status = fingerprint_audio(trigger.id, decoded.audio, database.fingerprints);
    if (!fingerprint_status.ok()) {
      return result(ExitCode::InternalFailure, {}, "fingerprint generation failed\n");
    }

    database.triggers.push_back(DatabaseTrigger{
        .trigger_id = trigger.id,
        .display_name = trigger.display_name,
        .duration_ms = static_cast<std::uint64_t>(
            (static_cast<unsigned long long>(decoded.audio.samples.size()) * 1000ULL) / kCanonicalSampleRate),
        .metadata_json = trigger.metadata_json,
    });
  }
  database.metadata.decoder_version = decoder_version.empty() ? "unknown-decoder" : decoder_version;
  database.metadata.build_report_json =
      "{\"builder\":\"" + std::string(kBuilderProfile) + "\",\"decoder\":\"" + database.metadata.decoder_version +
      "\",\"triggers\":" + std::to_string(database.triggers.size()) + ",\"fingerprints\":" +
      std::to_string(database.fingerprints.size()) + "}";

  const AmbiguityGateResult ambiguity = check_ambiguity(database);
  if (!ambiguity.accepted) {
    return result(ExitCode::Validation, {}, ambiguity.diagnostic_json + "\n");
  }
  database.metadata.build_report_json =
      "{\"builder\":\"" + std::string(kBuilderProfile) + "\",\"decoder\":\"" + database.metadata.decoder_version +
      "\",\"triggers\":" + std::to_string(database.triggers.size()) + ",\"fingerprints\":" +
      std::to_string(database.fingerprints.size()) + ",\"ambiguity\":" + ambiguity.diagnostic_json + "}";

  std::error_code ec;
  if (!output_path.parent_path().empty()) {
    std::filesystem::create_directories(output_path.parent_path(), ec);
    if (ec) {
      return result(ExitCode::OutputFailure, {}, "cannot create output directory\n");
    }
  }
  const std::filesystem::path temp_path = temp_output_path(output_path);
  std::filesystem::remove(temp_path, ec);
  const Status write_status = DatabaseWriter::write(temp_path, database);
  if (!write_status.ok()) {
    return result(ExitCode::OutputFailure, {}, "database write failed\n");
  }
  DatabaseReader reader;
  const Status open_status = reader.open(temp_path);
  if (!open_status.ok()) {
    std::filesystem::remove(temp_path, ec);
    return result(ExitCode::OutputFailure, {}, "written database did not verify\n");
  }
  const DatabaseIdentity identity = reader.identity();
  reader.close();
  std::filesystem::rename(temp_path, output_path, ec);
  if (ec) {
    std::filesystem::remove(temp_path, ec);
    return result(ExitCode::OutputFailure, {}, "cannot replace final output\n");
  }
  return result(ExitCode::Success, json_identity(identity));
}

CommandResult inspect_command(const std::filesystem::path& path) {
  DatabaseReader reader;
  const Status status = reader.open(path);
  if (!status.ok()) {
    return result(ExitCode::Validation, {}, "database open failed\n");
  }
  return result(ExitCode::Success, json_identity(reader.identity()));
}

CommandResult verify_command(const std::filesystem::path& path, bool release) {
  DatabaseReader reader;
  const Status status = reader.open(path);
  if (!status.ok()) {
    return result(ExitCode::Validation, {}, "database verify failed\n");
  }
  if (release) {
    return result(ExitCode::Validation, {}, "database was not built with frozen release toolchain\n");
  }
  return result(ExitCode::Success, "{\"ok\":true}\n");
}

}  // namespace

CommandResult run_database_command(const std::vector<std::string>& argv, AudioDecoder* decoder) {
  if (argv.size() < 2U) {
    return result(ExitCode::Validation, {}, "missing command\n");
  }
  FfmpegAudioDecoder default_decoder;
  AudioDecoder& active_decoder = decoder == nullptr ? static_cast<AudioDecoder&>(default_decoder) : *decoder;

  const std::string& command = argv[1];
  if (command == "build") {
    if (argv.size() != 4U) {
      return result(ExitCode::Validation, {}, "usage: local_acr_db build <manifest.json> <output.lacrdb>\n");
    }
    return build_command(argv[2], argv[3], active_decoder);
  }
  if (command == "inspect") {
    if (argv.size() != 3U) {
      return result(ExitCode::Validation, {}, "usage: local_acr_db inspect <database.lacrdb>\n");
    }
    return inspect_command(argv[2]);
  }
  if (command == "verify") {
    if (argv.size() == 3U) {
      return verify_command(argv[2], false);
    }
    if (argv.size() == 4U && argv[2] == "--release") {
      return verify_command(argv[3], true);
    }
    return result(ExitCode::Validation, {}, "usage: local_acr_db verify [--release] <database.lacrdb>\n");
  }
  return result(ExitCode::Validation, {}, "unknown command\n");
}

}  // namespace local_acr::cli
