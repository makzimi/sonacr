#include "local_acr/version.h"

#include <cstdint>
#include <iostream>

int main() {
  const std::uint32_t actual = lacr_version_abi();
  if (actual == 1U) {
    return 0;
  }

  std::cerr << "expected ABI version 1, got " << actual << '\n';
  return 1;
}
