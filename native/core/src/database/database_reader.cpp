#include "database/database_reader.hpp"

#include "database/schema.hpp"
#include "database/semantic_digest.hpp"
#include "database/sqlite_api.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace local_acr {

namespace {

constexpr std::uintmax_t kMaxDatabaseBytes = 256ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kMaxTriggers = 100;
constexpr std::uint64_t kMaxFingerprints = 1000000;
constexpr std::uint64_t kMaxRowsPerTrigger = 100000;
constexpr std::uint64_t kMaxRowsPerHash = 128;
constexpr std::size_t kMaxMetadataValueBytes = 64U * 1024U;
constexpr std::size_t kMaxTriggerMetadataBytes = 16U * 1024U;

class Statement final {
 public:
  Statement(sqlite3* db, const char* sql) noexcept {
    if (sqlite3_prepare_v2(db, sql, -1, &stmt_, nullptr) != SQLITE_OK) {
      stmt_ = nullptr;
    }
  }

  ~Statement() {
    if (stmt_ != nullptr) {
      sqlite3_finalize(stmt_);
    }
  }

  [[nodiscard]] bool valid() const noexcept {
    return stmt_ != nullptr;
  }

  [[nodiscard]] sqlite3_stmt* get() const noexcept {
    return stmt_;
  }

 private:
  sqlite3_stmt* stmt_ = nullptr;
};

std::string column_text(sqlite3_stmt* stmt, int column) {
  const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, column));
  const int bytes = sqlite3_column_bytes(stmt, column);
  if (text == nullptr || bytes < 0) {
    return {};
  }
  return std::string(text, static_cast<std::size_t>(bytes));
}

Status expect_single_ok(sqlite3* db, const char* sql, std::string_view expected) noexcept {
  Statement stmt(db, sql);
  if (!stmt.valid()) {
    return Status::native_engine_failure();
  }
  if (sqlite3_step(stmt.get()) != SQLITE_ROW) {
    return Status::invalid_argument();
  }
  if (column_text(stmt.get(), 0) != expected) {
    return Status::invalid_argument();
  }
  return Status::ok_status();
}

Status expect_no_rows(sqlite3* db, const char* sql) noexcept {
  Statement stmt(db, sql);
  if (!stmt.valid()) {
    return Status::native_engine_failure();
  }
  const int code = sqlite3_step(stmt.get());
  if (code == SQLITE_DONE) {
    return Status::ok_status();
  }
  if (code == SQLITE_ROW) {
    return Status::invalid_argument();
  }
  return sqlite_status(code);
}

Status query_count(sqlite3* db, const char* sql, std::uint64_t& out) noexcept {
  Statement stmt(db, sql);
  if (!stmt.valid()) {
    return Status::native_engine_failure();
  }
  if (sqlite3_step(stmt.get()) != SQLITE_ROW) {
    return Status::invalid_argument();
  }
  const sqlite3_int64 value = sqlite3_column_int64(stmt.get(), 0);
  if (value < 0) {
    return Status::invalid_argument();
  }
  out = static_cast<std::uint64_t>(value);
  return Status::ok_status();
}

Status load_metadata(sqlite3* db, std::map<std::string, std::string>& out) noexcept {
  Statement stmt(db, "SELECT key,value FROM database_metadata ORDER BY key COLLATE BINARY");
  if (!stmt.valid()) {
    return Status::native_engine_failure();
  }
  while (true) {
    const int code = sqlite3_step(stmt.get());
    if (code == SQLITE_DONE) {
      break;
    }
    if (code != SQLITE_ROW) {
      return sqlite_status(code);
    }
    const std::string key = column_text(stmt.get(), 0);
    const std::string value = column_text(stmt.get(), 1);
    if (value.size() > kMaxMetadataValueBytes) {
      return Status::resource_limit_exceeded();
    }
    out[key] = value;
  }
  return Status::ok_status();
}

Status load_triggers(sqlite3* db, std::vector<DatabaseTrigger>& out) noexcept {
  Statement stmt(
      db,
      "SELECT trigger_id,display_name,duration_ms,metadata_json FROM triggers ORDER BY trigger_id");
  if (!stmt.valid()) {
    return Status::native_engine_failure();
  }
  while (true) {
    const int code = sqlite3_step(stmt.get());
    if (code == SQLITE_DONE) {
      break;
    }
    if (code != SQLITE_ROW) {
      return sqlite_status(code);
    }
    DatabaseTrigger trigger{
        .trigger_id = column_text(stmt.get(), 0),
        .display_name = column_text(stmt.get(), 1),
        .duration_ms = static_cast<std::uint64_t>(sqlite3_column_int64(stmt.get(), 2)),
        .metadata_json = column_text(stmt.get(), 3),
    };
    if (trigger.metadata_json.size() > kMaxTriggerMetadataBytes || trigger.duration_ms == 0) {
      return Status::invalid_argument();
    }
    out.push_back(std::move(trigger));
  }
  return Status::ok_status();
}

Status load_fingerprints(sqlite3* db, std::vector<FingerprintRow>& out) noexcept {
  Statement stmt(db,
                 "SELECT trigger_id,hash,time_frame FROM fingerprints "
                 "ORDER BY trigger_id,hash,time_frame");
  if (!stmt.valid()) {
    return Status::native_engine_failure();
  }
  while (true) {
    const int code = sqlite3_step(stmt.get());
    if (code == SQLITE_DONE) {
      break;
    }
    if (code != SQLITE_ROW) {
      return sqlite_status(code);
    }
    const sqlite3_int64 hash = sqlite3_column_int64(stmt.get(), 1);
    const sqlite3_int64 time = sqlite3_column_int64(stmt.get(), 2);
    if (hash < 0 || hash > 16777215 || time < 0 || time > UINT32_MAX) {
      return Status::invalid_argument();
    }
    out.push_back(FingerprintRow{
        .trigger_id = column_text(stmt.get(), 0),
        .landmark = Landmark{static_cast<std::uint32_t>(hash), static_cast<std::uint32_t>(time)},
    });
  }
  return Status::ok_status();
}

Status validate_group_bound(sqlite3* db, const char* sql, std::uint64_t max_count) noexcept {
  Statement stmt(db, sql);
  if (!stmt.valid()) {
    return Status::native_engine_failure();
  }
  while (true) {
    const int code = sqlite3_step(stmt.get());
    if (code == SQLITE_DONE) {
      return Status::ok_status();
    }
    if (code != SQLITE_ROW) {
      return sqlite_status(code);
    }
    const sqlite3_int64 count = sqlite3_column_int64(stmt.get(), 0);
    if (count < 0 || static_cast<std::uint64_t>(count) > max_count) {
      return Status::resource_limit_exceeded();
    }
  }
}

Status validate_metadata_required(const std::map<std::string, std::string>& metadata) noexcept {
  const auto require = [&](std::string_view key) -> const std::string* {
    const auto found = metadata.find(std::string(key));
    return found == metadata.end() ? nullptr : &found->second;
  };
  const std::string* schema_version = require("schema_version");
  const std::string* fingerprint_profile = require("fingerprint_profile");
  const std::string* matcher_profile = require("matcher_profile");
  const std::string* digest = require("content_digest_sha256");
  if (schema_version == nullptr || fingerprint_profile == nullptr || matcher_profile == nullptr ||
      digest == nullptr || require("database_id") == nullptr || require("database_version") == nullptr ||
      require("build_report_json") == nullptr || require("decoder_version") == nullptr) {
    return Status::invalid_argument();
  }
  if (*schema_version != "1" || *fingerprint_profile != "landmark-v1" ||
      *matcher_profile != "conservative-v1" || digest->size() != 64U) {
    return Status::invalid_argument();
  }
  return Status::ok_status();
}

}  // namespace

DatabaseReader::~DatabaseReader() {
  close();
}

Status DatabaseReader::open(const std::filesystem::path& path) noexcept {
  close();
  std::error_code ec;
  if (std::filesystem::is_symlink(std::filesystem::symlink_status(path, ec)) || ec) {
    return Status::invalid_argument();
  }
  if (!std::filesystem::is_regular_file(path, ec) || ec) {
    return Status::invalid_argument();
  }
  const std::uintmax_t size = std::filesystem::file_size(path, ec);
  if (ec || size > kMaxDatabaseBytes) {
    return Status::resource_limit_exceeded();
  }

  sqlite3* db = nullptr;
  if (sqlite3_open_v2(path.string().c_str(), &db, SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX,
                      nullptr) != SQLITE_OK ||
      db == nullptr) {
    if (db != nullptr) {
      sqlite3_close(db);
    }
    return Status::native_engine_failure();
  }
  db_ = db;

  Status status = sqlite_exec(db_, "PRAGMA query_only=ON; PRAGMA foreign_keys=ON;");
  if (status.ok()) {
    status = expect_single_ok(db_, "PRAGMA integrity_check", "ok");
  }
  if (status.ok()) {
    status = expect_no_rows(db_, "PRAGMA foreign_key_check");
  }
  std::map<std::string, std::string> metadata;
  if (status.ok()) {
    status = load_metadata(db_, metadata);
  }
  if (status.ok()) {
    status = validate_metadata_required(metadata);
  }

  std::uint64_t trigger_count = 0;
  std::uint64_t fingerprint_count = 0;
  if (status.ok()) {
    status = query_count(db_, "SELECT COUNT(*) FROM triggers", trigger_count);
  }
  if (status.ok()) {
    status = query_count(db_, "SELECT COUNT(*) FROM fingerprints", fingerprint_count);
  }
  if (status.ok() && (trigger_count > kMaxTriggers || fingerprint_count > kMaxFingerprints)) {
    status = Status::resource_limit_exceeded();
  }
  if (status.ok()) {
    status = validate_group_bound(
        db_, "SELECT COUNT(*) FROM fingerprints GROUP BY trigger_id", kMaxRowsPerTrigger);
  }
  if (status.ok()) {
    status = validate_group_bound(db_, "SELECT COUNT(*) FROM fingerprints GROUP BY hash", kMaxRowsPerHash);
  }

  std::vector<DatabaseTrigger> triggers;
  std::vector<FingerprintRow> fingerprints;
  if (status.ok()) {
    status = load_triggers(db_, triggers);
  }
  if (status.ok()) {
    status = load_fingerprints(db_, fingerprints);
  }
  if (status.ok()) {
    SemanticDatabase semantic{
        .metadata = DatabaseMetadata{
            .schema_version = metadata["schema_version"],
            .fingerprint_profile = metadata["fingerprint_profile"],
            .matcher_profile = metadata["matcher_profile"],
            .database_id = metadata["database_id"],
            .database_version = metadata["database_version"],
            .build_report_json = metadata["build_report_json"],
            .decoder_version = metadata["decoder_version"],
        },
        .triggers = std::move(triggers),
        .fingerprints = std::move(fingerprints),
    };
    const std::string digest = hex_sha256(compute_semantic_digest(semantic));
    if (digest != metadata["content_digest_sha256"]) {
      status = Status::invalid_argument();
    } else {
      identity_ = DatabaseIdentity{
          .database_id = semantic.metadata.database_id,
          .database_version = semantic.metadata.database_version,
          .fingerprint_profile = semantic.metadata.fingerprint_profile,
          .matcher_profile = semantic.metadata.matcher_profile,
          .content_digest_sha256 = digest,
          .trigger_count = trigger_count,
          .fingerprint_count = fingerprint_count,
      };
    }
  }

  if (!status.ok()) {
    close();
  }
  return status;
}

void DatabaseReader::close() noexcept {
  if (db_ != nullptr) {
    sqlite3_close(db_);
    db_ = nullptr;
  }
  identity_ = {};
}

const DatabaseIdentity& DatabaseReader::identity() const noexcept {
  return identity_;
}

Status DatabaseReader::try_debug_write_for_test() noexcept {
  if (db_ == nullptr) {
    return Status::invalid_state();
  }
  const Status status = sqlite_exec(db_, "CREATE TABLE should_not_write(x INTEGER);");
  return status.ok() ? Status::native_engine_failure() : Status::invalid_state();
}

}  // namespace local_acr
