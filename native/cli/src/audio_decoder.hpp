#ifndef LOCAL_ACR_CLI_AUDIO_DECODER_HPP
#define LOCAL_ACR_CLI_AUDIO_DECODER_HPP

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace local_acr::cli {

struct FfmpegDecoderPlan final {
  std::vector<std::string> ffprobe_argv;
  std::vector<std::string> ffmpeg_argv;
  std::uint64_t max_decoded_pcm_bytes = 0;
};

struct ProcessResult final {
  bool started = false;
  int exit_code = -1;
  bool stdout_limit_exceeded = false;
  std::vector<std::uint8_t> stdout_bytes;
};

[[nodiscard]] bool is_supported_audio_extension(std::string_view path);

[[nodiscard]] FfmpegDecoderPlan make_ffmpeg_decoder_plan(const std::filesystem::path& ffprobe,
                                                         const std::filesystem::path& ffmpeg,
                                                         const std::filesystem::path& input);

[[nodiscard]] ProcessResult run_process_capture_stdout(const std::vector<std::string>& argv,
                                                       std::size_t max_stdout_bytes);

}  // namespace local_acr::cli

#endif
