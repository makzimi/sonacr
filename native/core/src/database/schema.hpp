#ifndef LOCAL_ACR_DATABASE_SCHEMA_HPP
#define LOCAL_ACR_DATABASE_SCHEMA_HPP

#include <string_view>

namespace local_acr {

inline constexpr std::string_view kDatabaseSchemaSql =
    "CREATE TABLE database_metadata (\n"
    "  key TEXT PRIMARY KEY,\n"
    "  value TEXT NOT NULL\n"
    ") WITHOUT ROWID;\n"
    "\n"
    "CREATE TABLE triggers (\n"
    "  trigger_id TEXT PRIMARY KEY,\n"
    "  display_name TEXT NOT NULL,\n"
    "  duration_ms INTEGER NOT NULL CHECK(duration_ms > 0),\n"
    "  metadata_json TEXT NOT NULL\n"
    ") WITHOUT ROWID;\n"
    "\n"
    "CREATE TABLE fingerprints (\n"
    "  trigger_id TEXT NOT NULL,\n"
    "  hash INTEGER NOT NULL CHECK(hash BETWEEN 0 AND 16777215),\n"
    "  time_frame INTEGER NOT NULL CHECK(time_frame >= 0),\n"
    "  PRIMARY KEY(trigger_id, hash, time_frame),\n"
    "  FOREIGN KEY(trigger_id) REFERENCES triggers(trigger_id)\n"
    ") WITHOUT ROWID;\n"
    "\n"
    "CREATE INDEX fingerprints_by_hash\n"
    "  ON fingerprints(hash, trigger_id, time_frame);\n";

}  // namespace local_acr

#endif
