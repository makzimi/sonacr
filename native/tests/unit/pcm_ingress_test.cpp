#include "audio/pcm_ingress.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <span>
#include <string_view>
#include <vector>

namespace {

using local_acr::MonoPcmView;
using local_acr::MonoSink;
using local_acr::PcmIngress;
using local_acr::PcmPlane;
using local_acr::PcmView;
using local_acr::SampleFormat;
using local_acr::Status;
using local_acr::StatusCode;

int failures = 0;

void check(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

void check_near(float actual, float expected, std::string_view message) {
  check(std::fabs(actual - expected) <= 1.0e-6F, message);
}

class CapturingSink final : public MonoSink {
 public:
  Status consume(MonoPcmView pcm) noexcept override {
    ++calls;
    first_source_frames.push_back(pcm.first_source_frame);
    sample_rates.push_back(pcm.sample_rate);
    samples.insert(samples.end(), pcm.samples.begin(), pcm.samples.end());
    return Status::ok_status();
  }

  std::size_t calls = 0;
  std::vector<std::uint64_t> first_source_frames;
  std::vector<std::uint32_t> sample_rates;
  std::vector<float> samples;
};

template <typename Sample, std::size_t Extent>
PcmPlane plane(const std::span<const Sample, Extent> samples) {
  return PcmPlane{samples.data(), samples.size_bytes()};
}

void s16_mono_is_normalized() {
  std::array<float, 3> scratch{};
  PcmIngress ingress(scratch);
  CapturingSink sink;
  const std::array<std::int16_t, 3> input{-32768, 0, 32767};
  const std::array planes{plane(std::span{input})};

  const Status status = ingress.push(
      PcmView{planes, 3, 1, 48000, 10, SampleFormat::S16Interleaved}, sink);

  check(status.ok(), "S16 mono status");
  check(sink.calls == 1, "S16 mono emits once");
  check(sink.samples.size() == 3, "S16 mono frame count");
  check_near(sink.samples[0], -1.0F, "S16 minimum maps to -1");
  check_near(sink.samples[1], 0.0F, "S16 zero maps to zero");
  check_near(sink.samples[2], 32767.0F / 32768.0F, "S16 maximum normalization");
  check(sink.first_source_frames == std::vector<std::uint64_t>{10}, "S16 source frame");
  check(sink.sample_rates == std::vector<std::uint32_t>{48000}, "S16 source rate");
}

void s16_stereo_is_averaged_in_channel_order() {
  std::array<float, 2> scratch{};
  PcmIngress ingress(scratch);
  CapturingSink sink;
  const std::array<std::int16_t, 4> input{32767, -32768, -32768, -32768};
  const std::array planes{plane(std::span{input})};

  const Status status = ingress.push(
      PcmView{planes, 2, 2, 48000, 0, SampleFormat::S16Interleaved}, sink);

  check(status.ok(), "S16 stereo status");
  check_near(sink.samples[0], -1.0F / 65536.0F, "S16 stereo averages after normalization");
  check_near(sink.samples[1], -1.0F, "S16 stereo minimum average");
}

void interleaved_channels_are_averaged_and_clamped() {
  std::array<float, 2> scratch{};
  PcmIngress ingress(scratch);
  CapturingSink sink;
  const std::array<float, 4> input{2.0F, 2.0F, -2.0F, -2.0F};
  const std::array planes{plane(std::span{input})};

  const Status status = ingress.push(
      PcmView{planes, 2, 2, 44100, 0, SampleFormat::F32Interleaved}, sink);

  check(status.ok(), "F32 interleaved status");
  check_near(sink.samples[0], 1.0F, "positive mix clamps to one");
  check_near(sink.samples[1], -1.0F, "negative mix clamps to minus one");
}

void planar_channels_are_averaged_in_channel_order() {
  std::array<float, 2> scratch{};
  PcmIngress ingress(scratch);
  CapturingSink sink;
  const std::array<float, 2> left{0.25F, -1.0F};
  const std::array<float, 2> right{0.75F, 1.0F};
  const std::array planes{plane(std::span{left}), plane(std::span{right})};

  const Status status = ingress.push(
      PcmView{planes, 2, 2, 48000, 0, SampleFormat::F32Planar}, sink);

  check(status.ok(), "F32 planar status");
  check_near(sink.samples[0], 0.5F, "planar frame zero average");
  check_near(sink.samples[1], 0.0F, "planar frame one average");
}

void malformed_batches_emit_nothing() {
  std::array<float, 4> scratch{};
  PcmIngress ingress(scratch);
  CapturingSink sink;
  const std::array<float, 4> finite{0.0F, 0.0F, 0.0F, 0.0F};
  const std::array<float, 4> nonfinite{0.0F, 0.0F, std::numeric_limits<float>::quiet_NaN(), 0.0F};
  const std::array one_plane{plane(std::span{finite})};
  const std::array two_planes{plane(std::span{finite}), plane(std::span{finite})};
  const std::array nan_plane{plane(std::span{nonfinite})};
  const std::array<PcmPlane, 1> short_plane{PcmPlane{finite.data(), sizeof(float)}};

  check(ingress.push(PcmView{two_planes, 2, 2, 48000, 0, SampleFormat::F32Interleaved}, sink).code() ==
            StatusCode::InvalidArgument,
        "interleaved input requires one plane");
  check(ingress.push(PcmView{one_plane, 2, 2, 48000, 0, SampleFormat::F32Planar}, sink).code() ==
            StatusCode::InvalidArgument,
        "planar input requires one plane per channel");
  check(ingress.push(PcmView{one_plane, 0, 1, 48000, 0, SampleFormat::F32Interleaved}, sink).code() ==
            StatusCode::InvalidArgument,
        "zero frames are rejected");
  check(ingress.push(PcmView{one_plane, 2, 0, 48000, 0, SampleFormat::F32Interleaved}, sink).code() ==
            StatusCode::InvalidArgument,
        "zero channels are rejected");
  check(ingress.push(PcmView{one_plane, 2, 9, 48000, 0, SampleFormat::F32Interleaved}, sink).code() ==
            StatusCode::InvalidArgument,
        "more than eight channels are rejected");
  check(ingress.push(PcmView{one_plane, 2, 1, 7999, 0, SampleFormat::F32Interleaved}, sink).code() ==
            StatusCode::InvalidArgument,
        "sample rates below 8 kHz are rejected");
  check(ingress.push(PcmView{one_plane, 2, 1, 192001, 0, SampleFormat::F32Interleaved}, sink).code() ==
            StatusCode::InvalidArgument,
        "sample rates above 192 kHz are rejected");
  check(ingress.push(PcmView{short_plane, 2, 1, 48000, 0, SampleFormat::F32Interleaved}, sink).code() ==
            StatusCode::InvalidArgument,
        "short planes are rejected");
  check(ingress.push(PcmView{nan_plane, 2, 2, 48000, 0, SampleFormat::F32Interleaved}, sink).code() ==
            StatusCode::InvalidArgument,
        "nonfinite PCM is rejected");
  check(ingress.push(PcmView{one_plane, 2, 1, 48000, 0, static_cast<SampleFormat>(99)}, sink).code() ==
            StatusCode::InvalidArgument,
        "unknown formats are rejected");
  check(sink.calls == 0, "malformed inputs never reach the sink");
}

void rejected_samples_do_not_advance_timeline() {
  std::array<float, 2> scratch{};
  PcmIngress ingress(scratch);
  CapturingSink sink;
  const std::array<float, 2> finite{0.25F, 0.5F};
  const std::array<float, 2> nonfinite{std::numeric_limits<float>::infinity(), 0.0F};
  const std::array finite_plane{plane(std::span{finite})};
  const std::array nonfinite_plane{plane(std::span{nonfinite})};

  check(ingress.push(PcmView{finite_plane, 2, 1, 48000, 0, SampleFormat::F32Interleaved}, sink).ok(),
        "transaction baseline succeeds");
  check(ingress.push(PcmView{nonfinite_plane, 2, 1, 48000, 2, SampleFormat::F32Interleaved}, sink).code() ==
            StatusCode::InvalidArgument,
        "transaction rejects nonfinite batch");
  check(ingress.push(PcmView{finite_plane, 2, 1, 48000, 2, SampleFormat::F32Interleaved}, sink).ok(),
        "corrected batch reuses the unadvanced timeline");
  check(sink.calls == 2, "rejected batch produces no partial sink call");
}

void capacity_and_timeline_are_enforced_until_reset() {
  std::array<float, 2> scratch{};
  PcmIngress ingress(scratch);
  CapturingSink sink;
  const std::array<float, 3> three_frames{0.0F, 0.0F, 0.0F};
  const std::array<float, 2> two_frames{0.25F, 0.5F};
  const std::array three_plane{plane(std::span{three_frames})};
  const std::array two_plane{plane(std::span{two_frames})};

  check(ingress.push(PcmView{three_plane, 3, 1, 48000, 100, SampleFormat::F32Interleaved}, sink).code() ==
            StatusCode::ResourceLimitExceeded,
        "batch larger than scratch is rejected");
  check(ingress.push(PcmView{two_plane, 2, 1, 48000, 100, SampleFormat::F32Interleaved}, sink).ok(),
        "first timeline batch succeeds");
  check(ingress.push(PcmView{two_plane, 2, 1, 48000, 102, SampleFormat::F32Interleaved}, sink).ok(),
        "adjacent timeline batch succeeds");
  check(ingress.push(PcmView{two_plane, 2, 1, 48000, 105, SampleFormat::F32Interleaved}, sink).code() ==
            StatusCode::AudioDiscontinuity,
        "timeline gap is rejected");
  check(ingress.push(PcmView{two_plane, 2, 1, 48000, 104, SampleFormat::F32Interleaved}, sink).code() ==
            StatusCode::AudioDiscontinuity,
        "discontinuity remains terminal until reset");
  check(sink.calls == 2, "discontinuous input does not reach sink");

  ingress.reset();
  check(ingress.push(PcmView{two_plane, 2, 1, 44100, 7, SampleFormat::F32Interleaved}, sink).ok(),
        "reset permits a new shape and timeline");
}

void shape_changes_are_discontinuities() {
  std::array<float, 2> scratch{};
  PcmIngress ingress(scratch);
  CapturingSink sink;
  const std::array<std::int16_t, 2> s16{0, 0};
  const std::array<float, 2> f32{0.0F, 0.0F};
  const std::array s16_plane{plane(std::span{s16})};
  const std::array f32_plane{plane(std::span{f32})};

  check(ingress.push(PcmView{s16_plane, 2, 1, 48000, 0, SampleFormat::S16Interleaved}, sink).ok(),
        "shape baseline succeeds");
  check(ingress.push(PcmView{f32_plane, 2, 1, 48000, 2, SampleFormat::F32Interleaved}, sink).code() ==
            StatusCode::AudioDiscontinuity,
        "format change is a discontinuity");
}

std::vector<float> process_partitioned(const std::span<const float> input,
                                       const std::span<const std::size_t> chunks) {
  std::array<float, 64> scratch{};
  PcmIngress ingress(scratch);
  CapturingSink sink;
  std::size_t offset = 0;

  for (const std::size_t frames : chunks) {
    const std::span<const float> batch = input.subspan(offset * 2, frames * 2);
    const std::array planes{plane(batch)};
    const Status status = ingress.push(
        PcmView{planes,
                static_cast<std::uint32_t>(frames),
                2,
                48000,
                static_cast<std::uint64_t>(offset),
                SampleFormat::F32Interleaved},
        sink);
    check(status.ok(), "partitioned batch succeeds");
    offset += frames;
  }

  check(offset * 2 == input.size(), "partition covers every input sample");
  return sink.samples;
}

void chunk_partitions_are_deterministic() {
  std::array<float, 32> input{};
  for (std::size_t index = 0; index < input.size(); ++index) {
    input[index] = static_cast<float>(index) / 32.0F - 0.5F;
  }

  const std::array<std::size_t, 1> one_chunk{16};
  const std::array<std::size_t, 5> many_chunks{1, 3, 2, 7, 3};
  const std::vector<float> expected = process_partitioned(input, one_chunk);
  const std::vector<float> actual = process_partitioned(input, many_chunks);

  check(actual == expected, "chunk partitions produce identical mono samples");

  std::uint32_t state = 0x4C414352U;
  for (std::size_t trial = 0; trial < 32; ++trial) {
    std::vector<std::size_t> chunks;
    std::size_t remaining = 16;
    while (remaining > 0) {
      state = state * 1664525U + 1013904223U;
      const std::size_t proposed = static_cast<std::size_t>(state % 7U) + 1U;
      const std::size_t frames = std::min(proposed, remaining);
      chunks.push_back(frames);
      remaining -= frames;
    }
    check(process_partitioned(input, chunks) == expected,
          "deterministic randomized partition matches one batch");
  }
}

}  // namespace

int main() {
  s16_mono_is_normalized();
  s16_stereo_is_averaged_in_channel_order();
  interleaved_channels_are_averaged_and_clamped();
  planar_channels_are_averaged_in_channel_order();
  malformed_batches_emit_nothing();
  rejected_samples_do_not_advance_timeline();
  capacity_and_timeline_are_enforced_until_reset();
  shape_changes_are_discontinuities();
  chunk_partitions_are_deterministic();
  return failures == 0 ? 0 : 1;
}
