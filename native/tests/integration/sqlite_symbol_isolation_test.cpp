#include "database/database_reader.hpp"
#include "database/database_writer.hpp"

#include <sqlite3.h>

#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

namespace {

int failures = 0;

void check(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

std::filesystem::path temp_path() {
  return std::filesystem::temp_directory_path() / "local-acr" / "sqlite-symbol-isolation.lacrdb";
}

local_acr::SemanticDatabase database_fixture() {
  return local_acr::SemanticDatabase{
      .metadata = local_acr::DatabaseMetadata{
          .database_id = "symbol-isolation",
          .database_version = "1",
          .build_report_json = "{}",
          .decoder_version = "test",
      },
      .triggers =
          {
              local_acr::DatabaseTrigger{
                  .trigger_id = "trigger",
                  .display_name = "Trigger",
                  .duration_ms = 1000,
                  .metadata_json = "{}",
              },
          },
      .fingerprints =
          {
              local_acr::FingerprintRow{
                  .trigger_id = "trigger",
                  .landmark = local_acr::Landmark{0x00010203U, 7U},
              },
          },
  };
}

void system_sqlite_can_open_memory_database() {
  sqlite3* db = nullptr;
  check(sqlite3_libversion_number() > 0, "system SQLite library is linked");
  check(sqlite3_open(":memory:", &db) == SQLITE_OK && db != nullptr,
        "system SQLite opens an in-memory database");
  if (db != nullptr) {
    sqlite3_close(db);
  }
}

void local_database_core_still_operates() {
  std::filesystem::create_directories(temp_path().parent_path());
  std::filesystem::remove(temp_path());
  check(local_acr::DatabaseWriter::write(temp_path(), database_fixture()).ok(),
        "local database writer works in system-SQLite-linked executable");
  local_acr::DatabaseReader reader;
  check(reader.open(temp_path()).ok(), "local database reader works in system-SQLite-linked executable");
}

}  // namespace

int main() {
  system_sqlite_can_open_memory_database();
  local_database_core_still_operates();
  return failures == 0 ? 0 : 1;
}
