#include "database/semantic_digest.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace local_acr {

namespace {

using MetadataRow = std::pair<std::string, std::string>;

void update_bytes(Sha256& sha, std::string_view bytes) {
  sha.update(std::span<const std::uint8_t>{
      reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()});
}

void update_u8(Sha256& sha, std::uint8_t value) {
  sha.update(std::span<const std::uint8_t, 1>{&value, 1});
}

void update_u32(Sha256& sha, std::uint32_t value) {
  std::array<std::uint8_t, 4> bytes{
      static_cast<std::uint8_t>(value >> 24U),
      static_cast<std::uint8_t>(value >> 16U),
      static_cast<std::uint8_t>(value >> 8U),
      static_cast<std::uint8_t>(value),
  };
  sha.update(bytes);
}

void update_u64(Sha256& sha, std::uint64_t value) {
  std::array<std::uint8_t, 8> bytes{};
  for (std::size_t i = 0; i < bytes.size(); ++i) {
    bytes[i] = static_cast<std::uint8_t>(value >> ((7U - i) * 8U));
  }
  sha.update(bytes);
}

void update_length_prefixed(Sha256& sha, const std::string& value) {
  update_u32(sha, static_cast<std::uint32_t>(value.size()));
  update_bytes(sha, value);
}

std::vector<MetadataRow> metadata_rows(const DatabaseMetadata& metadata) {
  return {
      {"build_report_json", metadata.build_report_json},
      {"database_id", metadata.database_id},
      {"database_version", metadata.database_version},
      {"decoder_version", metadata.decoder_version},
      {"fingerprint_profile", metadata.fingerprint_profile},
      {"matcher_profile", metadata.matcher_profile},
      {"schema_version", metadata.schema_version},
  };
}

}  // namespace

std::array<std::uint8_t, 32> compute_semantic_digest(SemanticDatabase database) {
  Sha256 sha;
  update_bytes(sha, "LACRDB-DIGEST");
  update_u8(sha, 0);
  update_u32(sha, 1);

  std::vector<MetadataRow> meta = metadata_rows(database.metadata);
  std::sort(meta.begin(), meta.end(), [](const MetadataRow& lhs, const MetadataRow& rhs) {
    return lhs.first < rhs.first;
  });
  for (const MetadataRow& row : meta) {
    update_u8(sha, 0x01U);
    update_length_prefixed(sha, row.first);
    update_length_prefixed(sha, row.second);
  }

  std::sort(database.triggers.begin(), database.triggers.end(),
            [](const DatabaseTrigger& lhs, const DatabaseTrigger& rhs) {
              return lhs.trigger_id < rhs.trigger_id;
            });
  for (const DatabaseTrigger& trigger : database.triggers) {
    update_u8(sha, 0x02U);
    update_length_prefixed(sha, trigger.trigger_id);
    update_length_prefixed(sha, trigger.display_name);
    update_u64(sha, trigger.duration_ms);
    update_length_prefixed(sha, trigger.metadata_json);
  }

  std::sort(database.fingerprints.begin(), database.fingerprints.end(),
            [](const FingerprintRow& lhs, const FingerprintRow& rhs) {
              if (lhs.trigger_id != rhs.trigger_id) {
                return lhs.trigger_id < rhs.trigger_id;
              }
              if (lhs.landmark.hash != rhs.landmark.hash) {
                return lhs.landmark.hash < rhs.landmark.hash;
              }
              return lhs.landmark.anchor_time_frame < rhs.landmark.anchor_time_frame;
            });
  for (const FingerprintRow& fingerprint : database.fingerprints) {
    update_u8(sha, 0x03U);
    update_length_prefixed(sha, fingerprint.trigger_id);
    update_u32(sha, fingerprint.landmark.hash);
    update_u32(sha, fingerprint.landmark.anchor_time_frame);
  }

  update_u8(sha, 0xFFU);
  return sha.finish();
}

std::string hex_sha256(const std::array<std::uint8_t, 32>& digest) {
  return hex_sha256(std::span<const std::uint8_t, 32>{digest});
}

}  // namespace local_acr
