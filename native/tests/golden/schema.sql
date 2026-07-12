CREATE TABLE database_metadata (
  key TEXT PRIMARY KEY,
  value TEXT NOT NULL
) WITHOUT ROWID;

CREATE TABLE triggers (
  trigger_id TEXT PRIMARY KEY,
  display_name TEXT NOT NULL,
  duration_ms INTEGER NOT NULL CHECK(duration_ms > 0),
  metadata_json TEXT NOT NULL
) WITHOUT ROWID;

CREATE TABLE fingerprints (
  trigger_id TEXT NOT NULL,
  hash INTEGER NOT NULL CHECK(hash BETWEEN 0 AND 16777215),
  time_frame INTEGER NOT NULL CHECK(time_frame >= 0),
  PRIMARY KEY(trigger_id, hash, time_frame),
  FOREIGN KEY(trigger_id) REFERENCES triggers(trigger_id)
) WITHOUT ROWID;

CREATE INDEX fingerprints_by_hash
  ON fingerprints(hash, trigger_id, time_frame);
