#ifndef LOCAL_ACR_DATABASE_DATABASE_TYPES_HPP
#define LOCAL_ACR_DATABASE_DATABASE_TYPES_HPP

#include "fingerprint/landmark.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace local_acr {

struct DatabaseMetadata final {
  std::string schema_version = "1";
  std::string fingerprint_profile = "landmark-v1";
  std::string matcher_profile = "conservative-v1";
  std::string database_id;
  std::string database_version;
  std::string build_report_json;
  std::string decoder_version;
};

struct DatabaseTrigger final {
  std::string trigger_id;
  std::string display_name;
  std::uint64_t duration_ms = 0;
  std::string metadata_json;
};

struct FingerprintRow final {
  std::string trigger_id;
  Landmark landmark;
};

struct SemanticDatabase final {
  DatabaseMetadata metadata;
  std::vector<DatabaseTrigger> triggers;
  std::vector<FingerprintRow> fingerprints;
};

struct DatabaseIdentity final {
  std::string database_id;
  std::string database_version;
  std::string fingerprint_profile;
  std::string matcher_profile;
  std::string content_digest_sha256;
  std::uint64_t trigger_count = 0;
  std::uint64_t fingerprint_count = 0;
};

}  // namespace local_acr

#endif
