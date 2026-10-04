#include "fingerprint/landmark_builder.hpp"

#include <cstdint>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

namespace {

using local_acr::ConfirmedPeak;
using local_acr::Landmark;
using local_acr::LandmarkBuilder;
using local_acr::LandmarkSink;
using local_acr::Status;

int failures = 0;

void check(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

class CapturingLandmarkSink final : public LandmarkSink {
 public:
  Status consume(std::span<const Landmark> batch) noexcept override {
    landmarks.insert(landmarks.end(), batch.begin(), batch.end());
    return Status::ok_status();
  }

  std::vector<Landmark> landmarks;
};

ConfirmedPeak peak(std::uint32_t time_frame, std::uint8_t bin) {
  return ConfirmedPeak{time_frame, bin, 0};
}

std::uint32_t hash(std::uint8_t anchor_bin, std::uint8_t target_bin, std::uint32_t delta_time) {
  return static_cast<std::uint32_t>(anchor_bin) |
         (static_cast<std::uint32_t>(target_bin) << 8U) |
         (delta_time << 16U);
}

void hash_layout_boundaries_and_rejection_rules() {
  LandmarkBuilder builder;
  CapturingLandmarkSink sink;
  const std::vector<ConfirmedPeak> peaks{
      peak(0, 1), peak(4, 33), peak(10, 223), peak(106, 255), peak(107, 250)};

  check(builder.process(peaks, sink).ok(), "boundary peaks process");
  check(builder.finish_finite(sink).ok(), "boundary finite finish");

  check(sink.landmarks.size() == 2, "only valid delta/bin pairs are emitted");
  if (sink.landmarks.size() >= 2) {
    check(sink.landmarks[0] == Landmark{hash(1, 33, 4), 0}, "delta 4 and bin delta 32 hash");
    check((sink.landmarks[0].hash >> 24U) == 0, "upper hash byte is reserved");
    check(sink.landmarks[1] == Landmark{hash(223, 255, 96), 10}, "delta 96 and bin 255 hash");
  }
}

void target_ordering_and_three_target_cap_are_stable() {
  LandmarkBuilder builder;
  CapturingLandmarkSink sink;
  const std::vector<ConfirmedPeak> peaks{
      peak(10, 50), peak(14, 51), peak(14, 52), peak(15, 49), peak(16, 48), peak(17, 47)};

  check(builder.process(peaks, sink).ok(), "ordered peaks process");
  check(builder.finish_finite(sink).ok(), "ordered finite finish");

  check(sink.landmarks.size() == 3, "anchor emits at most three targets");
  if (sink.landmarks.size() == 3) {
    check(sink.landmarks[0] == Landmark{hash(50, 51, 4), 10}, "same-delta targets sort by bin");
    check(sink.landmarks[1] == Landmark{hash(50, 52, 4), 10}, "second same-delta target");
    check(sink.landmarks[2] == Landmark{hash(50, 49, 5), 10}, "then next delta target");
  }
}

void duplicate_identity_retains_first_occurrence() {
  LandmarkBuilder builder;
  CapturingLandmarkSink sink;
  const std::vector<ConfirmedPeak> peaks{
      peak(0, 10), peak(4, 20), peak(4, 20), peak(8, 20)};

  check(builder.process(peaks, sink).ok(), "duplicate peaks process");
  check(builder.finish_finite(sink).ok(), "duplicate finite finish");

  std::size_t identity_count = 0;
  for (const Landmark& landmark : sink.landmarks) {
    if (landmark == Landmark{hash(10, 20, 4), 0}) {
      ++identity_count;
    }
  }
  check(identity_count == 1, "duplicate landmark identity is retained once");
}

void anchor_expiry_waits_until_all_96_hop_targets_pass() {
  LandmarkBuilder builder;
  CapturingLandmarkSink sink;

  check(builder.process(std::vector<ConfirmedPeak>{peak(0, 100)}, sink).ok(), "anchor only");
  check(builder.retained_peak_count() == 1, "anchor retained initially");
  check(builder.process(std::vector<ConfirmedPeak>{peak(97, 100)}, sink).ok(), "post-window target");
  check(builder.retained_peak_count() == 1, "expired anchor removed after 96-hop window");
  check(builder.finish_finite(sink).ok(), "expiry finite finish");
  check(sink.landmarks.empty(), "post-window target does not pair with expired anchor");
}

void unordered_peaks_are_rejected_without_state_advance() {
  LandmarkBuilder builder;
  CapturingLandmarkSink sink;
  check(builder.process(std::vector<ConfirmedPeak>{peak(10, 20)}, sink).ok(), "baseline peak");
  check(builder.process(std::vector<ConfirmedPeak>{peak(9, 21)}, sink).code() ==
            local_acr::StatusCode::AudioDiscontinuity,
        "time regression is rejected");
  check(builder.retained_peak_count() == 1, "rejected batch does not advance state");
}

void long_streams_do_not_exhaust_duplicate_tracking() {
  LandmarkBuilder builder;
  CapturingLandmarkSink sink;
  bool all_ok = true;
  for (std::uint32_t time = 0; time < 2000U; ++time) {
    const std::vector<ConfirmedPeak> frame_peaks{peak(time, 10), peak(time, 20)};
    all_ok = builder.process(frame_peaks, sink).ok() && all_ok;
  }
  check(all_ok, "two peaks per frame for 2000 frames process without resource exhaustion");
  check(sink.landmarks.size() > 1536U, "emission continues past the duplicate-tracking capacity");
}

}  // namespace

int main() {
  long_streams_do_not_exhaust_duplicate_tracking();
  hash_layout_boundaries_and_rejection_rules();
  target_ordering_and_three_target_cap_are_stable();
  duplicate_identity_retains_first_occurrence();
  anchor_expiry_waits_until_all_96_hop_targets_pass();
  unordered_peaks_are_rejected_without_state_advance();
  return failures == 0 ? 0 : 1;
}
