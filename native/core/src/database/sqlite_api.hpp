#ifndef LOCAL_ACR_DATABASE_SQLITE_API_HPP
#define LOCAL_ACR_DATABASE_SQLITE_API_HPP

#include "support/status.hpp"

#include "sqlite3.h"

#include <string_view>

namespace local_acr {

[[nodiscard]] Status sqlite_status(int code) noexcept;
[[nodiscard]] Status sqlite_exec(sqlite3* db, std::string_view sql) noexcept;

}  // namespace local_acr

#endif
