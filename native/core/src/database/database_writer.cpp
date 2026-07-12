#include "database/database_writer.hpp"

#include "database/schema.hpp"
#include "database/semantic_digest.hpp"
#include "database/sqlite_api.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace local_acr {

namespace {

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

Status bind_text(sqlite3_stmt* stmt, int index, std::string_view value) noexcept {
  return sqlite_status(sqlite3_bind_text(stmt, index, value.data(), static_cast<int>(value.size()),
                                         SQLITE_TRANSIENT));
}

Status bind_i64(sqlite3_stmt* stmt, int index, std::uint64_t value) noexcept {
  if (value > static_cast<std::uint64_t>(INT64_MAX)) {
    return Status::invalid_argument();
  }
  return sqlite_status(sqlite3_bind_int64(stmt, index, static_cast<sqlite3_int64>(value)));
}

Status step_done(sqlite3_stmt* stmt) noexcept {
  const int code = sqlite3_step(stmt);
  if (code != SQLITE_DONE) {
    return sqlite_status(code);
  }
  sqlite3_reset(stmt);
  sqlite3_clear_bindings(stmt);
  return Status::ok_status();
}

Status insert_metadata(sqlite3* db, std::string_view key, std::string_view value) noexcept {
  Statement stmt(db, "INSERT INTO database_metadata(key,value) VALUES(?,?)");
  if (!stmt.valid()) {
    return Status::native_engine_failure();
  }
  Status status = bind_text(stmt.get(), 1, key);
  if (status.ok()) {
    status = bind_text(stmt.get(), 2, value);
  }
  if (status.ok()) {
    status = step_done(stmt.get());
  }
  return status;
}

std::vector<DatabaseTrigger> sorted_triggers(std::vector<DatabaseTrigger> triggers) {
  std::sort(triggers.begin(), triggers.end(), [](const DatabaseTrigger& lhs, const DatabaseTrigger& rhs) {
    return lhs.trigger_id < rhs.trigger_id;
  });
  return triggers;
}

std::vector<FingerprintRow> sorted_fingerprints(std::vector<FingerprintRow> rows) {
  std::sort(rows.begin(), rows.end(), [](const FingerprintRow& lhs, const FingerprintRow& rhs) {
    if (lhs.trigger_id != rhs.trigger_id) {
      return lhs.trigger_id < rhs.trigger_id;
    }
    if (lhs.landmark.hash != rhs.landmark.hash) {
      return lhs.landmark.hash < rhs.landmark.hash;
    }
    return lhs.landmark.anchor_time_frame < rhs.landmark.anchor_time_frame;
  });
  return rows;
}

Status insert_trigger(sqlite3* db, const DatabaseTrigger& trigger) noexcept {
  Statement stmt(db,
                 "INSERT INTO triggers(trigger_id,display_name,duration_ms,metadata_json) "
                 "VALUES(?,?,?,?)");
  if (!stmt.valid()) {
    return Status::native_engine_failure();
  }
  Status status = bind_text(stmt.get(), 1, trigger.trigger_id);
  if (status.ok()) {
    status = bind_text(stmt.get(), 2, trigger.display_name);
  }
  if (status.ok()) {
    status = bind_i64(stmt.get(), 3, trigger.duration_ms);
  }
  if (status.ok()) {
    status = bind_text(stmt.get(), 4, trigger.metadata_json);
  }
  if (status.ok()) {
    status = step_done(stmt.get());
  }
  return status;
}

Status insert_fingerprint(sqlite3* db, const FingerprintRow& row) noexcept {
  Statement stmt(db,
                 "INSERT INTO fingerprints(trigger_id,hash,time_frame) "
                 "VALUES(?,?,?)");
  if (!stmt.valid()) {
    return Status::native_engine_failure();
  }
  Status status = bind_text(stmt.get(), 1, row.trigger_id);
  if (status.ok()) {
    status = bind_i64(stmt.get(), 2, row.landmark.hash);
  }
  if (status.ok()) {
    status = bind_i64(stmt.get(), 3, row.landmark.anchor_time_frame);
  }
  if (status.ok()) {
    status = step_done(stmt.get());
  }
  return status;
}

Status write_database(sqlite3* db, const SemanticDatabase& database) noexcept {
  Status status = sqlite_exec(db, "PRAGMA page_size=4096; PRAGMA auto_vacuum=NONE; PRAGMA foreign_keys=ON;");
  if (status.ok()) {
    status = sqlite_exec(db, "PRAGMA application_id=1279341394; PRAGMA user_version=1;");
  }
  if (status.ok()) {
    status = sqlite_exec(db, kDatabaseSchemaSql);
  }
  if (!status.ok()) {
    return status;
  }

  status = insert_metadata(db, "schema_version", database.metadata.schema_version);
  if (status.ok()) {
    status = insert_metadata(db, "fingerprint_profile", database.metadata.fingerprint_profile);
  }
  if (status.ok()) {
    status = insert_metadata(db, "matcher_profile", database.metadata.matcher_profile);
  }
  if (status.ok()) {
    status = insert_metadata(db, "database_id", database.metadata.database_id);
  }
  if (status.ok()) {
    status = insert_metadata(db, "database_version", database.metadata.database_version);
  }
  if (status.ok()) {
    status = insert_metadata(db, "build_report_json", database.metadata.build_report_json);
  }
  if (status.ok()) {
    status = insert_metadata(db, "decoder_version", database.metadata.decoder_version);
  }
  if (!status.ok()) {
    return status;
  }

  for (const DatabaseTrigger& trigger : sorted_triggers(database.triggers)) {
    status = insert_trigger(db, trigger);
    if (!status.ok()) {
      return status;
    }
  }
  for (const FingerprintRow& row : sorted_fingerprints(database.fingerprints)) {
    status = insert_fingerprint(db, row);
    if (!status.ok()) {
      return status;
    }
  }

  const std::string digest = hex_sha256(compute_semantic_digest(database));
  status = insert_metadata(db, "content_digest_sha256", digest);
  if (status.ok()) {
    status = sqlite_exec(db, "VACUUM; PRAGMA journal_mode=DELETE;");
  }
  return status;
}

}  // namespace

Status sqlite_status(const int code) noexcept {
  if (code == SQLITE_OK || code == SQLITE_DONE || code == SQLITE_ROW) {
    return Status::ok_status();
  }
  if (code == SQLITE_CONSTRAINT || code == SQLITE_MISMATCH || code == SQLITE_RANGE) {
    return Status::invalid_argument();
  }
  if (code == SQLITE_READONLY) {
    return Status::invalid_state();
  }
  return Status::native_engine_failure();
}

Status sqlite_exec(sqlite3* db, std::string_view sql) noexcept {
  std::string text(sql);
  char* error = nullptr;
  const int code = sqlite3_exec(db, text.c_str(), nullptr, nullptr, &error);
  if (error != nullptr) {
    sqlite3_free(error);
  }
  return sqlite_status(code);
}

Status DatabaseWriter::write(const std::filesystem::path& path,
                             const SemanticDatabase& database) noexcept {
  std::error_code ec;
  std::filesystem::remove(path, ec);
  sqlite3* db = nullptr;
  const int open_code = sqlite3_open_v2(path.string().c_str(), &db,
                                       SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
                                           SQLITE_OPEN_FULLMUTEX,
                                       nullptr);
  if (open_code != SQLITE_OK || db == nullptr) {
    if (db != nullptr) {
      sqlite3_close(db);
    }
    return Status::native_engine_failure();
  }
  Status status = write_database(db, database);
  const int close_code = sqlite3_close(db);
  if (status.ok() && close_code != SQLITE_OK) {
    status = Status::native_engine_failure();
  }
  return status;
}

Status DatabaseWriter::delete_metadata_for_test(const std::filesystem::path& path,
                                                std::string_view key) noexcept {
  sqlite3* db = nullptr;
  if (sqlite3_open_v2(path.string().c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_FULLMUTEX,
                      nullptr) != SQLITE_OK ||
      db == nullptr) {
    if (db != nullptr) {
      sqlite3_close(db);
    }
    return Status::native_engine_failure();
  }
  Statement stmt(db, "DELETE FROM database_metadata WHERE key=?");
  Status status = stmt.valid() ? bind_text(stmt.get(), 1, key) : Status::native_engine_failure();
  if (status.ok()) {
    status = step_done(stmt.get());
  }
  sqlite3_close(db);
  return status;
}

Status DatabaseWriter::insert_fingerprint_for_test(const std::filesystem::path& path,
                                                   const FingerprintRow& row) noexcept {
  sqlite3* db = nullptr;
  if (sqlite3_open_v2(path.string().c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_FULLMUTEX,
                      nullptr) != SQLITE_OK ||
      db == nullptr) {
    if (db != nullptr) {
      sqlite3_close(db);
    }
    return Status::native_engine_failure();
  }
  Status status = sqlite_exec(db, "PRAGMA foreign_keys=OFF;");
  if (status.ok()) {
    status = insert_fingerprint(db, row);
  }
  sqlite3_close(db);
  return status;
}

}  // namespace local_acr
