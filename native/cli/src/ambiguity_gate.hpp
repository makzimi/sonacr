#ifndef LOCAL_ACR_CLI_AMBIGUITY_GATE_HPP
#define LOCAL_ACR_CLI_AMBIGUITY_GATE_HPP

#include "database/database_types.hpp"

#include <string>
#include <vector>

namespace local_acr::cli {

struct AmbiguityGateResult final {
  bool accepted = true;
  std::string diagnostic_json;
  std::vector<std::string> ambiguous_pairs;
  std::uint32_t checked_windows = 0;
};

[[nodiscard]] AmbiguityGateResult check_ambiguity(const SemanticDatabase& database);

}  // namespace local_acr::cli

#endif
