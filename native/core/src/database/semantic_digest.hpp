#ifndef LOCAL_ACR_DATABASE_SEMANTIC_DIGEST_HPP
#define LOCAL_ACR_DATABASE_SEMANTIC_DIGEST_HPP

#include "database/database_types.hpp"
#include "support/sha256.hpp"

#include <array>
#include <cstdint>
#include <string>

namespace local_acr {

[[nodiscard]] std::array<std::uint8_t, 32> compute_semantic_digest(SemanticDatabase database);
[[nodiscard]] std::string hex_sha256(const std::array<std::uint8_t, 32>& digest);

}  // namespace local_acr

#endif
