#include "audio_decoder.hpp"

#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

namespace {

int failures = 0;

void check(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

void ffmpeg_plan_uses_argv_not_shell() {
  const std::filesystem::path suspicious = "/tmp/song;rm -rf nope.mp3";
  const local_acr::cli::FfmpegDecoderPlan plan =
      local_acr::cli::make_ffmpeg_decoder_plan("/opt/bin/ffprobe", "/opt/bin/ffmpeg", suspicious);
  check(plan.ffprobe_argv[0] == "/opt/bin/ffprobe", "ffprobe executable is argv[0]");
  check(plan.ffmpeg_argv[0] == "/opt/bin/ffmpeg", "ffmpeg executable is argv[0]");
  check(plan.ffmpeg_argv.back() == "-", "ffmpeg writes raw PCM to stdout");
  bool found_input = false;
  for (const std::string& arg : plan.ffmpeg_argv) {
    if (arg == suspicious.string()) {
      found_input = true;
    }
    check(arg.find(";rm -rf") == std::string::npos || arg == suspicious.string(),
          "suspicious text is preserved only in input argv element");
  }
  check(found_input, "input path is passed as a single argv element");
}

void extension_admission_is_deterministic() {
  check(local_acr::cli::is_supported_audio_extension("cue.wav"), "wav accepted");
  check(local_acr::cli::is_supported_audio_extension("cue.WAV"), "upper wav accepted");
  check(local_acr::cli::is_supported_audio_extension("cue.mp3"), "mp3 accepted");
  check(!local_acr::cli::is_supported_audio_extension("cue.flac"), "flac rejected for MVP");
}

void decoded_output_bound_is_reported() {
  const local_acr::cli::FfmpegDecoderPlan plan =
      local_acr::cli::make_ffmpeg_decoder_plan("ffprobe", "ffmpeg", "/tmp/a.wav");
  check(plan.max_decoded_pcm_bytes == 60ULL * 60ULL * 11025ULL * sizeof(float),
        "decoded PCM bound matches 60 aggregate minutes at canonical mono float");
}

void process_runner_executes_argv_without_shell() {
  const local_acr::cli::ProcessResult result =
      local_acr::cli::run_process_capture_stdout({"/bin/echo", "hello;exit 99"}, 128U);
  check(result.started, "process runner starts executable");
  check(result.exit_code == 0, "shell metacharacters are not executed");
  check(std::string(result.stdout_bytes.begin(), result.stdout_bytes.end()) == "hello;exit 99\n",
        "shell metacharacters stay inside one argv element");
  check(!result.stdout_limit_exceeded, "bounded output not exceeded");
}

void process_runner_enforces_stdout_bound() {
  const local_acr::cli::ProcessResult result =
      local_acr::cli::run_process_capture_stdout({"/bin/echo", "abcdef"}, 3U);
  check(result.started, "bounded process starts executable");
  check(result.stdout_limit_exceeded, "stdout limit is reported");
  check(result.stdout_bytes.size() == 3U, "stdout is capped at requested bound");
}

}  // namespace

int main() {
  ffmpeg_plan_uses_argv_not_shell();
  extension_admission_is_deterministic();
  decoded_output_bound_is_reported();
  process_runner_executes_argv_without_shell();
  process_runner_enforces_stdout_bound();
  return failures == 0 ? 0 : 1;
}
