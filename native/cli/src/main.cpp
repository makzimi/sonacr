#include "database_commands.hpp"

#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
  std::vector<std::string> args;
  args.reserve(static_cast<std::size_t>(argc));
  for (int i = 0; i < argc; ++i) {
    args.emplace_back(argv[i]);
  }
  const local_acr::cli::CommandResult result = local_acr::cli::run_database_command(args);
  std::cout << result.stdout_text;
  std::cerr << result.stderr_text;
  return static_cast<int>(result.exit_code);
}
