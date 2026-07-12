#ifndef LOCAL_ACR_CLI_MANIFEST_HPP
#define LOCAL_ACR_CLI_MANIFEST_HPP

#include "support/status.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace local_acr::cli {

struct ManifestTrigger final {
  std::string id;
  std::string display_name;
  std::filesystem::path audio_path;
  std::string metadata_json;
};

struct ValidatedManifest final {
  std::string database_id;
  std::string database_version;
  std::vector<ManifestTrigger> triggers;
};

struct ManifestResult final {
  Status status = Status::ok_status();
  ValidatedManifest manifest;
};

[[nodiscard]] ManifestResult load_manifest(const std::filesystem::path& path);

}  // namespace local_acr::cli

#endif
