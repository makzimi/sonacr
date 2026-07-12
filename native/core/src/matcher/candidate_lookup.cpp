#include "matcher/candidate_lookup.hpp"

#include "database/sqlite_api.hpp"

#include <sqlite3.h>

#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace local_acr {

namespace {

constexpr std::size_t kMaxQueryLandmarks = 512;
constexpr std::size_t kMaxSqlHashes = 256;
constexpr std::uint64_t kMaxExpansions = 65536;

class Statement final {
 public:
  Statement(sqlite3* db, const std::string& sql) noexcept {
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt_, nullptr) != SQLITE_OK) {
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

std::string query_sql(const std::size_t count) {
  std::ostringstream sql;
  sql << "SELECT hash,trigger_id,time_frame FROM fingerprints WHERE hash IN (";
  for (std::size_t i = 0; i < count; ++i) {
    if (i != 0U) {
      sql << ',';
    }
    sql << '?';
  }
  sql << ") ORDER BY hash,trigger_id,time_frame";
  return sql.str();
}

using QueryByHash = std::map<std::uint32_t, std::vector<QueryLandmark>>;

QueryByHash group_query(std::span<const QueryLandmark> query) {
  QueryByHash grouped;
  for (const QueryLandmark& landmark : query) {
    if (landmark.hash > 16777215U) {
      return {};
    }
    grouped[landmark.hash].push_back(landmark);
  }
  return grouped;
}

std::vector<std::uint32_t> sorted_hashes(const QueryByHash& grouped) {
  std::vector<std::uint32_t> hashes;
  hashes.reserve(grouped.size());
  for (const auto& [hash, ignored] : grouped) {
    (void)ignored;
    hashes.push_back(hash);
  }
  return hashes;
}

Status process_chunk(sqlite3* db,
                     const std::vector<std::uint32_t>& hashes,
                     std::size_t begin,
                     std::size_t end,
                     const QueryByHash& query_by_hash,
                     OffsetAccumulator& accumulator,
                     std::uint64_t& expansions) {
  const std::size_t count = end - begin;
  Statement stmt(db, query_sql(count));
  if (!stmt.valid()) {
    return Status::native_engine_failure();
  }
  for (std::size_t index = 0; index < count; ++index) {
    const int bind_index = static_cast<int>(index + 1U);
    const std::uint32_t hash = hashes[begin + index];
    const Status status = sqlite_status(sqlite3_bind_int64(stmt.get(), bind_index, hash));
    if (!status.ok()) {
      return status;
    }
  }

  while (true) {
    const int code = sqlite3_step(stmt.get());
    if (code == SQLITE_DONE) {
      return Status::ok_status();
    }
    if (code != SQLITE_ROW) {
      return sqlite_status(code);
    }

    const sqlite3_int64 db_hash_i64 = sqlite3_column_int64(stmt.get(), 0);
    const sqlite3_int64 db_time_i64 = sqlite3_column_int64(stmt.get(), 2);
    if (db_hash_i64 < 0 || db_hash_i64 > 16777215 || db_time_i64 < 0 || db_time_i64 > UINT32_MAX) {
      return Status::invalid_argument();
    }
    const auto db_hash = static_cast<std::uint32_t>(db_hash_i64);
    const auto db_time = static_cast<std::uint32_t>(db_time_i64);
    const auto found = query_by_hash.find(db_hash);
    if (found == query_by_hash.end()) {
      return Status::native_engine_failure();
    }

    const std::string trigger_id = column_text(stmt.get(), 1);
    for (const QueryLandmark& query : found->second) {
      ++expansions;
      if (expansions >= kMaxExpansions) {
        return Status::resource_limit_exceeded();
      }
      const auto offset = static_cast<std::int64_t>(db_time) -
                          static_cast<std::int64_t>(query.time_frame);
      const Status status = accumulator.add_vote(CandidateVote{
          .trigger_id = trigger_id,
          .offset_frames = offset,
          .query_id = query.query_id,
      });
      if (!status.ok()) {
        return status;
      }
    }
  }
}

}  // namespace

CandidateLookupResult CandidateLookup::lookup(DatabaseReader& reader,
                                              std::span<const QueryLandmark> query) const noexcept {
  CandidateLookupResult result;
  if (query.empty() || query.size() > kMaxQueryLandmarks || reader.sqlite_handle_for_matcher() == nullptr) {
    result.status = Status::invalid_argument();
    return result;
  }

  QueryByHash query_by_hash = group_query(query);
  if (query_by_hash.empty()) {
    result.status = Status::invalid_argument();
    return result;
  }
  const std::vector<std::uint32_t> hashes = sorted_hashes(query_by_hash);

  OffsetAccumulator accumulator;
  std::uint64_t expansions = 0;
  for (std::size_t begin = 0; begin < hashes.size(); begin += kMaxSqlHashes) {
    const std::size_t end = std::min(begin + kMaxSqlHashes, hashes.size());
    result.status = process_chunk(reader.sqlite_handle_for_matcher(), hashes, begin, end, query_by_hash,
                                  accumulator, expansions);
    if (!result.status.ok()) {
      result.candidates.clear();
      return result;
    }
  }

  result.candidates = accumulator.finish(static_cast<std::uint32_t>(query.size()));
  return result;
}

}  // namespace local_acr
