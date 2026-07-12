#include "manifest.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

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
         ("manifest-test-" + std::to_string(static_cast<long long>(::getpid())));
}

void write_file(const std::filesystem::path& path, std::string_view contents) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary);
  output << contents;
}

std::filesystem::path make_audio(std::string_view name, std::size_t bytes = 16U) {
  const std::filesystem::path path = temp_dir() / name;
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary);
  for (std::size_t i = 0; i < bytes; ++i) {
    output.put(static_cast<char>('a' + (i % 26U)));
  }
  return path;
}

local_acr::cli::ManifestResult load_text(std::string_view text) {
  const std::filesystem::path manifest = temp_dir() / "manifest.json";
  write_file(manifest, text);
  return local_acr::cli::load_manifest(manifest);
}

void valid_manifest_preserves_metadata_and_paths() {
  make_audio("audio/welcome.mp3");
  const auto result = load_text(
      "{"
      "\"schemaVersion\":1,"
      "\"databaseId\":\"venue-demo\","
      "\"databaseVersion\":\"1.0.0\","
      "\"triggers\":[{"
      "\"id\":\"welcome-offer\","
      "\"displayName\":\"Welcome promotion\","
      "\"audio\":\"audio/welcome.mp3\","
      "\"metadata\":{\"title\":\"20% off\",\"body\":\"Show\"}"
      "}]"
      "}");
  check(result.status.ok(), "valid manifest loads");
  check(result.manifest.database_id == "venue-demo", "database id parsed");
  check(result.manifest.triggers.size() == 1U, "one trigger parsed");
  if (!result.manifest.triggers.empty()) {
    check(result.manifest.triggers[0].audio_path == std::filesystem::weakly_canonical(temp_dir() / "audio/welcome.mp3"),
          "audio path canonicalized");
    check(result.manifest.triggers[0].metadata_json == "{\"body\":\"Show\",\"title\":\"20% off\"}",
          "metadata is canonicalized by key");
  }
}

void rejects_duplicate_keys_and_unknown_top_level_fields() {
  make_audio("a.mp3");
  check(load_text("{\"schemaVersion\":1,\"schemaVersion\":1}").status.code() ==
            local_acr::StatusCode::InvalidArgument,
        "duplicate keys reject");
  check(load_text(
            "{\"schemaVersion\":1,\"databaseId\":\"db\",\"databaseVersion\":\"1\",\"unexpected\":1,"
            "\"triggers\":[]}")
            .status.code() == local_acr::StatusCode::InvalidArgument,
        "unknown top-level field rejects");
}

void rejects_missing_invalid_and_duplicate_trigger_ids() {
  make_audio("a.mp3");
  check(load_text("{\"schemaVersion\":1,\"databaseVersion\":\"1\",\"triggers\":[]}").status.code() ==
            local_acr::StatusCode::InvalidArgument,
        "missing databaseId rejects");
  check(load_text(
            "{\"schemaVersion\":1,\"databaseId\":\"bad id\",\"databaseVersion\":\"1\",\"triggers\":[]}")
            .status.code() == local_acr::StatusCode::InvalidArgument,
        "invalid database id rejects");
  check(load_text(
            "{\"schemaVersion\":1,\"databaseId\":\"db\",\"databaseVersion\":\"1\",\"triggers\":["
            "{\"id\":\"cue\",\"displayName\":\"Cue\",\"audio\":\"a.mp3\",\"metadata\":{}},"
            "{\"id\":\"cue\",\"displayName\":\"Cue 2\",\"audio\":\"a.mp3\",\"metadata\":{}}]}")
            .status.code() == local_acr::StatusCode::InvalidArgument,
        "duplicate trigger id rejects");
}

void rejects_invalid_utf8_in_strings() {
  make_audio("a.mp3");
  const std::string manifest =
      "{\"schemaVersion\":1,\"databaseId\":\"db\",\"databaseVersion\":\"1\",\"triggers\":["
      "{\"id\":\"cue\",\"displayName\":\"Cue " + std::string(1U, static_cast<char>(0xFF)) +
      "\",\"audio\":\"a.mp3\",\"metadata\":{}}]}";
  check(load_text(manifest).status.code() == local_acr::StatusCode::InvalidArgument,
        "invalid utf-8 in strings rejects");
}

void rejects_bad_audio_paths_and_files() {
  const std::filesystem::path outside = temp_dir().parent_path() / "outside.mp3";
  write_file(outside, "x");
  std::filesystem::create_directories(temp_dir() / "audio");
  std::error_code ec;
  std::filesystem::create_symlink(outside, temp_dir() / "audio/outside.mp3", ec);
  if (!ec) {
    check(load_text(
              "{\"schemaVersion\":1,\"databaseId\":\"db\",\"databaseVersion\":\"1\",\"triggers\":["
              "{\"id\":\"cue\",\"displayName\":\"Cue\",\"audio\":\"audio/outside.mp3\",\"metadata\":{}}]}")
              .status.code() == local_acr::StatusCode::InvalidArgument,
          "symlink escaping manifest dir rejects");
  }

  write_file(temp_dir() / "a.txt", "x");
  check(load_text(
            "{\"schemaVersion\":1,\"databaseId\":\"db\",\"databaseVersion\":\"1\",\"triggers\":["
            "{\"id\":\"cue\",\"displayName\":\"Cue\",\"audio\":\"a.txt\",\"metadata\":{}}]}")
            .status.code() == local_acr::StatusCode::InvalidArgument,
        "unsupported extension rejects");
}

void rejects_metadata_and_manifest_bounds() {
  make_audio("a.mp3");
  std::string large(17U * 1024U, 'x');
  const std::string manifest =
      "{\"schemaVersion\":1,\"databaseId\":\"db\",\"databaseVersion\":\"1\",\"triggers\":["
      "{\"id\":\"cue\",\"displayName\":\"Cue\",\"audio\":\"a.mp3\",\"metadata\":{\"blob\":\"" +
      large + "\"}}]}";
  check(load_text(manifest).status.code() == local_acr::StatusCode::ResourceLimitExceeded,
        "metadata above 16 KiB rejects");

  const std::filesystem::path big = temp_dir() / "big.json";
  write_file(big, std::string(1024U * 1024U + 1U, ' '));
  check(local_acr::cli::load_manifest(big).status.code() == local_acr::StatusCode::ResourceLimitExceeded,
        "manifest above 1 MiB rejects");
}

}  // namespace

int main() {
  std::filesystem::remove_all(temp_dir());
  valid_manifest_preserves_metadata_and_paths();
  rejects_duplicate_keys_and_unknown_top_level_fields();
  rejects_missing_invalid_and_duplicate_trigger_ids();
  rejects_invalid_utf8_in_strings();
  rejects_bad_audio_paths_and_files();
  rejects_metadata_and_manifest_bounds();
  return failures == 0 ? 0 : 1;
}
