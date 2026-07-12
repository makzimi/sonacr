#ifndef LOCAL_ACR_DATABASE_DATABASE_WRITER_HPP
#define LOCAL_ACR_DATABASE_DATABASE_WRITER_HPP

#include "database/database_types.hpp"
#include "support/status.hpp"

#include <filesystem>
#include <string_view>

namespace local_acr {

class DatabaseWriter final {
 public:
  [[nodiscard]] static Status write(const std::filesystem::path& path,
                                    const SemanticDatabase& database) noexcept;
  [[nodiscard]] static Status delete_metadata_for_test(const std::filesystem::path& path,
                                                       std::string_view key) noexcept;
  [[nodiscard]] static Status insert_fingerprint_for_test(const std::filesystem::path& path,
                                                          const FingerprintRow& row) noexcept;
};

}  // namespace local_acr

#endif
