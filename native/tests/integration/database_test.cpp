#include "database/database_reader.hpp"
#include "database/database_writer.hpp"
#include "database/schema.hpp"
#include "database/semantic_digest.hpp"
#include "fingerprint/landmark.hpp"

#include <sqlite3.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using local_acr::DatabaseIdentity;
using local_acr::DatabaseMetadata;
using local_acr::DatabaseReader;
using local_acr::DatabaseTrigger;
using local_acr::DatabaseWriter;
using local_acr::FingerprintRow;
using local_acr::Landmark;
using local_acr::SemanticDatabase;
using local_acr::Status;
using local_acr::StatusCode;

int failures = 0;

void check(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

std::filesystem::path temp_path(std::string_view name) {
  return std::filesystem::temp_directory_path() / std::string("local-acr-") / std::string(name);
}

void ensure_temp_dir() {
  std::filesystem::create_directories(temp_path(""));
}

std::string read_file(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
}

std::filesystem::path source_root() {
  return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
}

DatabaseMetadata metadata() {
  return DatabaseMetadata{
      .database_id = "venue-demo",
      .database_version = "1.0.0",
      .build_report_json = "{\"warnings\":[]}",
      .decoder_version = "test-decoder",
  };
}

std::vector<DatabaseTrigger> triggers() {
  return {
      DatabaseTrigger{
          .trigger_id = "checkin-a",
          .display_name = "Cafe \xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82",
          .duration_ms = 1234,
          .metadata_json = "{\"promo\":1}",
      },
      DatabaseTrigger{
          .trigger_id = "music_b",
          .display_name = "Music B",
          .duration_ms = 4321,
          .metadata_json = "{}",
      },
  };
}

std::vector<FingerprintRow> rows() {
  return {
      FingerprintRow{.trigger_id = "music_b", .landmark = Landmark{0x00040102U, 14}},
      FingerprintRow{.trigger_id = "checkin-a", .landmark = Landmark{0x00040102U, 10}},
      FingerprintRow{.trigger_id = "checkin-a", .landmark = Landmark{0x0060FF01U, 99}},
  };
}

SemanticDatabase semantic_database(std::vector<DatabaseTrigger> trigger_rows,
                                   std::vector<FingerprintRow> fingerprint_rows) {
  return SemanticDatabase{
      .metadata = metadata(),
      .triggers = std::move(trigger_rows),
      .fingerprints = std::move(fingerprint_rows),
  };
}

void schema_matches_golden() {
  check(std::string(local_acr::kDatabaseSchemaSql) ==
            read_file(source_root() / "tests" / "golden" / "schema.sql"),
        "schema SQL matches golden file");
}

void semantic_digest_is_row_order_independent_and_utf8_stable() {
  SemanticDatabase a = semantic_database(triggers(), rows());
  std::vector<DatabaseTrigger> reversed_triggers = triggers();
  std::reverse(reversed_triggers.begin(), reversed_triggers.end());
  std::vector<FingerprintRow> reversed_rows = rows();
  std::reverse(reversed_rows.begin(), reversed_rows.end());
  SemanticDatabase b = semantic_database(std::move(reversed_triggers), std::move(reversed_rows));

  const std::array<std::uint8_t, 32> digest_a = local_acr::compute_semantic_digest(a);
  const std::array<std::uint8_t, 32> digest_b = local_acr::compute_semantic_digest(b);
  const std::string digest_hex = local_acr::hex_sha256(digest_a);

  check(digest_a == digest_b, "semantic digest is independent of row insertion order");
  if (digest_hex != "973aa1d881fa5ede2dc13601c09e4fb1549485b1f21aefa599016de9118d1fcb") {
    std::cerr << "digest fixture actual=" << digest_hex << '\n';
    check(false, "semantic digest UTF-8 fixture is pinned");
  }
}

void writer_creates_reader_validated_database() {
  ensure_temp_dir();
  const std::filesystem::path path = temp_path("valid.lacrdb");
  std::filesystem::remove(path);

  const SemanticDatabase database = semantic_database(triggers(), rows());
  check(DatabaseWriter::write(path, database).ok(), "database write succeeds");

  DatabaseReader reader;
  check(reader.open(path).ok(), "database reader opens written file");
  const DatabaseIdentity& identity = reader.identity();
  check(identity.database_id == "venue-demo", "reader exposes database id");
  check(identity.database_version == "1.0.0", "reader exposes database version");
  check(identity.trigger_count == 2, "reader validates trigger count");
  check(identity.fingerprint_count == 3, "reader validates fingerprint count");
  check(identity.content_digest_sha256 == local_acr::hex_sha256(local_acr::compute_semantic_digest(database)),
        "reader recomputes semantic digest");
}

void reader_rejects_invalid_profile_and_missing_digest() {
  ensure_temp_dir();
  const std::filesystem::path bad_profile = temp_path("bad-profile.lacrdb");
  std::filesystem::remove(bad_profile);
  SemanticDatabase database = semantic_database(triggers(), rows());
  database.metadata.fingerprint_profile = "other-profile";
  check(DatabaseWriter::write(bad_profile, database).ok(), "bad-profile fixture writes");
  DatabaseReader reader;
  check(reader.open(bad_profile).code() == StatusCode::InvalidArgument,
        "reader rejects unsupported fingerprint profile");

  const std::filesystem::path missing_digest = temp_path("missing-digest.lacrdb");
  std::filesystem::remove(missing_digest);
  database = semantic_database(triggers(), rows());
  check(DatabaseWriter::write(missing_digest, database).ok(), "missing digest fixture writes");
  check(DatabaseWriter::delete_metadata_for_test(missing_digest, "content_digest_sha256").ok(),
        "test removes digest row");
  check(reader.open(missing_digest).code() == StatusCode::InvalidArgument,
        "reader rejects missing digest row");
}

void reader_rejects_hash_posting_bound_and_foreign_key_corruption() {
  ensure_temp_dir();
  std::vector<FingerprintRow> many_rows;
  many_rows.reserve(129);
  for (std::uint32_t i = 0; i < 129; ++i) {
    many_rows.push_back(FingerprintRow{.trigger_id = "checkin-a", .landmark = Landmark{0x00010203U, i}});
  }
  const std::filesystem::path too_many = temp_path("too-many-postings.lacrdb");
  std::filesystem::remove(too_many);
  check(DatabaseWriter::write(too_many, semantic_database(triggers(), many_rows)).ok(),
        "too-many fixture writes");
  DatabaseReader reader;
  check(reader.open(too_many).code() == StatusCode::ResourceLimitExceeded,
        "reader rejects more than 128 postings per hash");

  const std::filesystem::path corrupt_fk = temp_path("foreign-key.lacrdb");
  std::filesystem::remove(corrupt_fk);
  check(DatabaseWriter::write(corrupt_fk, semantic_database(triggers(), rows())).ok(),
        "foreign-key fixture writes");
  check(DatabaseWriter::insert_fingerprint_for_test(
            corrupt_fk, FingerprintRow{.trigger_id = "missing", .landmark = Landmark{0x00000001U, 1}})
            .ok(),
        "test inserts foreign-key violating row");
  check(reader.open(corrupt_fk).code() == StatusCode::InvalidArgument,
        "reader rejects foreign key corruption");
}

void reader_uses_read_only_connection() {
  ensure_temp_dir();
  const std::filesystem::path path = temp_path("readonly.lacrdb");
  std::filesystem::remove(path);
  check(DatabaseWriter::write(path, semantic_database(triggers(), rows())).ok(),
        "readonly fixture writes");

  DatabaseReader reader;
  check(reader.open(path).ok(), "reader opens readonly fixture");
  check(reader.try_debug_write_for_test().code() == StatusCode::InvalidState,
        "reader connection is query-only read-only");
}

bool sqlite_compile_option_enabled(std::string_view expected) {
  sqlite3* db = nullptr;
  if (sqlite3_open_v2(":memory:", &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
                      nullptr) != SQLITE_OK ||
      db == nullptr) {
    if (db != nullptr) {
      sqlite3_close(db);
    }
    return false;
  }

  sqlite3_stmt* stmt = nullptr;
  bool found = false;
  if (sqlite3_prepare_v2(db, "PRAGMA compile_options", -1, &stmt, nullptr) == SQLITE_OK) {
    while (sqlite3_step(stmt) == SQLITE_ROW) {
      const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
      const int bytes = sqlite3_column_bytes(stmt, 0);
      if (text != nullptr && bytes >= 0 && std::string_view(text, static_cast<std::size_t>(bytes)) == expected) {
        found = true;
        break;
      }
    }
  }
  if (stmt != nullptr) {
    sqlite3_finalize(stmt);
  }
  sqlite3_close(db);
  return found;
}

void sqlite_compile_options_are_hardened() {
  check(sqlite_compile_option_enabled("THREADSAFE=1"), "SQLite is compiled serialized/threadsafe");
  check(sqlite_compile_option_enabled("DEFAULT_FOREIGN_KEYS"), "SQLite defaults foreign keys on");
  check(sqlite_compile_option_enabled("DQS=0"), "SQLite disables double-quoted string literals");
  check(sqlite_compile_option_enabled("OMIT_LOAD_EXTENSION"), "SQLite omits loadable extensions");
  check(sqlite_compile_option_enabled("OMIT_SHARED_CACHE"), "SQLite omits shared cache");
  check(sqlite_compile_option_enabled("USE_URI"), "SQLite URI handling is enabled");
}

}  // namespace

int main() {
  sqlite_compile_options_are_hardened();
  schema_matches_golden();
  semantic_digest_is_row_order_independent_and_utf8_stable();
  writer_creates_reader_validated_database();
  reader_rejects_invalid_profile_and_missing_digest();
  reader_rejects_hash_posting_bound_and_foreign_key_corruption();
  reader_uses_read_only_connection();
  return failures == 0 ? 0 : 1;
}
