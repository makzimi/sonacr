#ifndef LOCAL_ACR_DATABASE_DATABASE_READER_HPP
#define LOCAL_ACR_DATABASE_DATABASE_READER_HPP

#include "database/database_types.hpp"
#include "support/status.hpp"

#include <filesystem>

struct sqlite3;

namespace local_acr {

class DatabaseReader final {
 public:
  DatabaseReader() = default;
  ~DatabaseReader();

  DatabaseReader(const DatabaseReader&) = delete;
  DatabaseReader& operator=(const DatabaseReader&) = delete;

  [[nodiscard]] Status open(const std::filesystem::path& path) noexcept;
  void close() noexcept;

  [[nodiscard]] const DatabaseIdentity& identity() const noexcept;
  [[nodiscard]] Status try_debug_write_for_test() noexcept;

 private:
  sqlite3* db_ = nullptr;
  DatabaseIdentity identity_{};
};

}  // namespace local_acr

#endif
