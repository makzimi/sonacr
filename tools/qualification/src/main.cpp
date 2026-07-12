#include "qualification_runner.hpp"

#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
  std::vector<std::string> args;
  for (int index = 0; index < argc; ++index) {
    args.emplace_back(argv[index]);
  }
  if (args.size() != 4U || args[1] != "--manifest" ||
      (args[3] != "--dry-run" && args[3] != "--run-fixtures")) {
    std::cerr << "usage: local_acr_qualification --manifest <corpus.json> --dry-run|--run-fixtures\n";
    return 2;
  }
  const auto loaded = local_acr::qualification::load_corpus_manifest(args[2]);
  if (!loaded.status.ok()) {
    std::cerr << "invalid qualification corpus manifest\n";
    return 2;
  }
  if (args[3] == "--dry-run") {
    std::cout << local_acr::qualification::render_dry_run(loaded.manifest);
    return 0;
  }
  std::cout << local_acr::qualification::render_jsonl(
      local_acr::qualification::run_fixture_trials(loaded.manifest));
  return 0;
}
