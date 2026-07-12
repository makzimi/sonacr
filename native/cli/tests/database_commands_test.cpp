#include "database_commands.hpp"

#include "database/database_writer.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
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

std::filesystem::path temp_dir() {
  return std::filesystem::temp_directory_path() / "local-acr" /
         ("database-commands-test-" + std::to_string(static_cast<long long>(::getpid())));
}

void write_file(const std::filesystem::path& path, std::string_view contents) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary);
  output << contents;
}

std::string read_file(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
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

class FakeDecoder final : public local_acr::cli::AudioDecoder {
 public:
  local_acr::cli::DecodeResult decode(const std::filesystem::path& path,
                                      std::uint64_t max_pcm_bytes) override {
    requests.push_back(path);
    requested_bounds.push_back(max_pcm_bytes);
    return local_acr::cli::DecodeResult{
        .status = local_acr::Status::ok_status(),
        .audio =
            local_acr::cli::DecodedAudio{
                .samples = make_pcm(12U * 11025U),
                .sample_rate = 11025,
                .decoder_version = "fake-decoder-test",
            },
    };
  }

  std::vector<std::filesystem::path> requests;
  std::vector<std::uint64_t> requested_bounds;
};

std::filesystem::path make_audio(std::string_view name) {
  const std::filesystem::path path = temp_dir() / name;
  write_file(path, "placeholder");
  return path;
}

std::filesystem::path make_manifest() {
  make_audio("audio/cue.mp3");
  const std::filesystem::path manifest = temp_dir() / "manifest.json";
  write_file(
      manifest,
      "{\"schemaVersion\":1,\"databaseId\":\"venue-demo\",\"databaseVersion\":\"2026.07.12\","
      "\"triggers\":[{\"id\":\"checkin\",\"displayName\":\"Check-in promotion\","
      "\"audio\":\"audio/cue.mp3\",\"metadata\":{\"kind\":\"promo\"}}]}");
  return manifest;
}

std::filesystem::path make_ambiguous_manifest() {
  make_audio("audio/a.mp3");
  make_audio("audio/b.mp3");
  const std::filesystem::path manifest = temp_dir() / "ambiguous.json";
  write_file(
      manifest,
      "{\"schemaVersion\":1,\"databaseId\":\"venue-demo\",\"databaseVersion\":\"2026.07.12\","
      "\"triggers\":["
      "{\"id\":\"checkin_a\",\"displayName\":\"Check-in A\",\"audio\":\"audio/a.mp3\",\"metadata\":{}},"
      "{\"id\":\"checkin_b\",\"displayName\":\"Check-in B\",\"audio\":\"audio/b.mp3\",\"metadata\":{}}]}");
  return manifest;
}

void invalid_command_returns_stable_validation_exit() {
  FakeDecoder decoder;
  const local_acr::cli::CommandResult result =
      local_acr::cli::run_database_command({"local_acr_db", "missing"}, &decoder);
  check(result.exit_code == local_acr::cli::ExitCode::Validation, "unknown command uses validation exit");
}

void build_writes_database_durably_and_reports_metadata() {
  FakeDecoder decoder;
  const std::filesystem::path manifest = make_manifest();
  const std::filesystem::path output = temp_dir() / "out.lacrdb";
  write_file(output, "old-db");

  const local_acr::cli::CommandResult result =
      local_acr::cli::run_database_command({"local_acr_db", "build", manifest.string(), output.string()}, &decoder);

  check(result.exit_code == local_acr::cli::ExitCode::Success, "build command succeeds");
  check(decoder.requests.size() == 1U, "build decodes one trigger");
  check(decoder.requested_bounds.size() == 1U && decoder.requested_bounds[0] > 0U,
        "build passes decoded PCM bound to decoder");
  check(read_file(output) != "old-db", "build atomically replaces old final output");
  check(result.stdout_text.find("\"databaseId\":\"venue-demo\"") != std::string::npos,
        "build report includes database id");
  check(result.stdout_text.find("\"fingerprints\":") != std::string::npos,
        "build report includes fingerprint count");
}

void build_accepts_output_in_current_directory() {
  FakeDecoder decoder;
  const std::filesystem::path manifest = make_manifest();
  const std::filesystem::path old_cwd = std::filesystem::current_path();
  std::filesystem::current_path(temp_dir());
  const local_acr::cli::CommandResult result =
      local_acr::cli::run_database_command({"local_acr_db", "build", manifest.string(), "relative.lacrdb"},
                                           &decoder);
  std::filesystem::current_path(old_cwd);

  check(result.exit_code == local_acr::cli::ExitCode::Success, "build accepts output path in current directory");
  check(std::filesystem::is_regular_file(temp_dir() / "relative.lacrdb"), "relative output file is written");
}

void inspect_and_verify_read_built_database() {
  FakeDecoder decoder;
  const std::filesystem::path manifest = make_manifest();
  const std::filesystem::path output = temp_dir() / "inspect.lacrdb";
  check(local_acr::cli::run_database_command({"local_acr_db", "build", manifest.string(), output.string()},
                                             &decoder)
            .exit_code == local_acr::cli::ExitCode::Success,
        "fixture build succeeds");

  const local_acr::cli::CommandResult inspect =
      local_acr::cli::run_database_command({"local_acr_db", "inspect", output.string()}, &decoder);
  check(inspect.exit_code == local_acr::cli::ExitCode::Success, "inspect succeeds");
  check(inspect.stdout_text.find("\"databaseVersion\":\"2026.07.12\"") != std::string::npos,
        "inspect exposes database version");
  check(inspect.stdout_text.find("\"triggers\":1") != std::string::npos, "inspect exposes trigger count");

  const local_acr::cli::CommandResult verify =
      local_acr::cli::run_database_command({"local_acr_db", "verify", output.string()}, &decoder);
  check(verify.exit_code == local_acr::cli::ExitCode::Success, "ordinary verify succeeds");

  const local_acr::cli::CommandResult release_verify =
      local_acr::cli::run_database_command({"local_acr_db", "verify", "--release", output.string()}, &decoder);
  check(release_verify.exit_code == local_acr::cli::ExitCode::Validation,
        "release verify rejects non-frozen toolchain database");
}

void build_rejects_ambiguous_trigger_library_before_replacing_output() {
  FakeDecoder decoder;
  const std::filesystem::path manifest = make_ambiguous_manifest();
  const std::filesystem::path output = temp_dir() / "ambiguous-output.lacrdb";
  write_file(output, "old-db");

  const local_acr::cli::CommandResult result =
      local_acr::cli::run_database_command({"local_acr_db", "build", manifest.string(), output.string()}, &decoder);

  check(result.exit_code == local_acr::cli::ExitCode::Validation, "ambiguous build uses validation exit");
  check(result.stderr_text.find("checkin_a") != std::string::npos, "ambiguity diagnostic names first trigger");
  check(result.stderr_text.find("checkin_b") != std::string::npos, "ambiguity diagnostic names second trigger");
  check(read_file(output) == "old-db", "ambiguous build does not replace existing output");
}

void verify_corrupt_database_returns_validation() {
  FakeDecoder decoder;
  const std::filesystem::path corrupt = temp_dir() / "corrupt.lacrdb";
  write_file(corrupt, "not sqlite");
  const local_acr::cli::CommandResult verify =
      local_acr::cli::run_database_command({"local_acr_db", "verify", corrupt.string()}, &decoder);
  check(verify.exit_code == local_acr::cli::ExitCode::Validation, "corrupt database maps to validation exit");
}

}  // namespace

int main() {
  std::filesystem::remove_all(temp_dir());
  invalid_command_returns_stable_validation_exit();
  build_writes_database_durably_and_reports_metadata();
  build_accepts_output_in_current_directory();
  inspect_and_verify_read_built_database();
  build_rejects_ambiguous_trigger_library_before_replacing_output();
  verify_corrupt_database_returns_validation();
  return failures == 0 ? 0 : 1;
}
