#include "audio_decoder.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <csignal>
#include <cstdint>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace local_acr::cli {

namespace {

std::string lower_extension(std::string_view path) {
  std::string text(path);
  const std::size_t dot = text.find_last_of('.');
  if (dot == std::string::npos) {
    return {};
  }
  std::string ext = text.substr(dot);
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return ext;
}

}  // namespace

bool is_supported_audio_extension(std::string_view path) {
  const std::string ext = lower_extension(path);
  return ext == ".wav" || ext == ".mp3";
}

FfmpegDecoderPlan make_ffmpeg_decoder_plan(const std::filesystem::path& ffprobe,
                                           const std::filesystem::path& ffmpeg,
                                           const std::filesystem::path& input) {
  const std::string in = input.string();
  return FfmpegDecoderPlan{
      .ffprobe_argv =
          {
              ffprobe.string(),
              "-v",
              "error",
              "-show_format",
              "-show_streams",
              in,
          },
      .ffmpeg_argv =
          {
              ffmpeg.string(),
              "-nostdin",
              "-v",
              "error",
              "-i",
              in,
              "-map",
              "0:a:0",
              "-ac",
              "1",
              "-ar",
              "11025",
              "-f",
              "f32le",
              "-",
          },
      .max_decoded_pcm_bytes = 60ULL * 60ULL * 11025ULL * sizeof(float),
  };
}

ProcessResult run_process_capture_stdout(const std::vector<std::string>& argv, std::size_t max_stdout_bytes) {
  ProcessResult result;
  if (argv.empty()) {
    return result;
  }

  std::array<int, 2> stdout_pipe{-1, -1};
  if (::pipe(stdout_pipe.data()) != 0) {
    return result;
  }

  const pid_t child = ::fork();
  if (child < 0) {
    ::close(stdout_pipe[0]);
    ::close(stdout_pipe[1]);
    return result;
  }

  if (child == 0) {
    ::close(stdout_pipe[0]);
    if (::dup2(stdout_pipe[1], STDOUT_FILENO) < 0) {
      _exit(127);
    }
    ::close(stdout_pipe[1]);

    std::vector<char*> c_argv;
    c_argv.reserve(argv.size() + 1U);
    for (const std::string& arg : argv) {
      c_argv.push_back(const_cast<char*>(arg.c_str()));
    }
    c_argv.push_back(nullptr);
    ::execvp(c_argv[0], c_argv.data());
    _exit(127);
  }

  result.started = true;
  ::close(stdout_pipe[1]);

  std::array<std::uint8_t, 4096> buffer{};
  while (true) {
    const ssize_t read_count = ::read(stdout_pipe[0], buffer.data(), buffer.size());
    if (read_count == 0) {
      break;
    }
    if (read_count < 0) {
      break;
    }
    const auto count = static_cast<std::size_t>(read_count);
    const std::size_t remaining =
        result.stdout_bytes.size() < max_stdout_bytes ? max_stdout_bytes - result.stdout_bytes.size() : 0U;
    const std::size_t to_copy = std::min(remaining, count);
    result.stdout_bytes.insert(result.stdout_bytes.end(), buffer.begin(), buffer.begin() + to_copy);
    if (to_copy < count) {
      result.stdout_limit_exceeded = true;
      ::kill(child, SIGKILL);
      break;
    }
  }
  ::close(stdout_pipe[0]);

  int status = 0;
  while (::waitpid(child, &status, 0) < 0) {
    if (errno != EINTR) {
      return result;
    }
  }
  if (WIFEXITED(status)) {
    result.exit_code = WEXITSTATUS(status);
  } else if (WIFSIGNALED(status)) {
    result.exit_code = 128 + WTERMSIG(status);
  }
  return result;
}

}  // namespace local_acr::cli
