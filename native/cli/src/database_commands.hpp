#ifndef LOCAL_ACR_CLI_DATABASE_COMMANDS_HPP
#define LOCAL_ACR_CLI_DATABASE_COMMANDS_HPP

#include "exit_code.hpp"

#include "support/status.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace local_acr::cli {

struct DecodedAudio final {
  std::vector<float> samples;
  std::uint32_t sample_rate = 0;
  std::string decoder_version;
};

struct DecodeResult final {
  Status status = Status::ok_status();
  DecodedAudio audio;
};

class AudioDecoder {
 public:
  virtual ~AudioDecoder() = default;
  [[nodiscard]] virtual DecodeResult decode(const std::filesystem::path& path,
                                            std::uint64_t max_pcm_bytes) = 0;
};

struct CommandResult final {
  ExitCode exit_code = ExitCode::Success;
  std::string stdout_text;
  std::string stderr_text;
};

[[nodiscard]] CommandResult run_database_command(const std::vector<std::string>& argv,
                                                 AudioDecoder* decoder = nullptr);

}  // namespace local_acr::cli

#endif
