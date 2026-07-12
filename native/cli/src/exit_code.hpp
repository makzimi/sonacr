#ifndef LOCAL_ACR_CLI_EXIT_CODE_HPP
#define LOCAL_ACR_CLI_EXIT_CODE_HPP

namespace local_acr::cli {

enum class ExitCode : int {
  Success = 0,
  Validation = 2,
  DecodeFailure = 3,
  OutputFailure = 4,
  InternalFailure = 5,
};

}  // namespace local_acr::cli

#endif
