#include "fingerprint/gaussian_penalties_q16.hpp"
#include "fingerprint/peak_selector.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <span>
#include <string_view>
#include <vector>

namespace {

using local_acr::ConfirmedPeak;
using local_acr::DecisionSpectrum;
using local_acr::PeakSelector;
using local_acr::PeakSink;
using local_acr::Status;

int failures = 0;

void check(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

std::int32_t reference_round_q16(double value) {
  const double scaled = value * 65536.0;
  const double rounded = scaled >= 0.0 ? std::floor(scaled + 0.5) : std::ceil(scaled - 0.5);
  return static_cast<std::int32_t>(rounded);
}

class CapturingPeakSink final : public PeakSink {
 public:
  Status consume(std::span<const ConfirmedPeak> confirmed) noexcept override {
    peaks.insert(peaks.end(), confirmed.begin(), confirmed.end());
    return Status::ok_status();
  }

  std::vector<ConfirmedPeak> peaks;
};

DecisionSpectrum frame(std::uint64_t time_frame, std::int32_t fill_value = -100 * 65536) {
  DecisionSpectrum spectrum{};
  spectrum.time_frame = time_frame;
  spectrum.q16.fill(fill_value);
  return spectrum;
}

DecisionSpectrum quiet_frame(std::uint64_t time_frame) {
  return frame(time_frame, -101 * 65536);
}

void set_bin(DecisionSpectrum& spectrum, std::uint8_t bin, std::int32_t value) {
  spectrum.q16[static_cast<std::size_t>(bin - 1U)] = value;
}

void feed_warmup(PeakSelector& selector, CapturingPeakSink& sink) {
  for (std::uint64_t time = 0; time < PeakSelector::kWarmupFrames; ++time) {
    DecisionSpectrum spectrum = frame(time);
    check(selector.process(spectrum, sink).ok(), "warm-up frame succeeds");
  }
}

void generated_constants_are_checked() {
  check(local_acr::kThresholdDecayQ16 == reference_round_q16(std::log(0.934)),
        "threshold decay is round_q16(ln(0.934))");
  check(local_acr::kGaussianPenaltiesQ16.size() == 255, "Gaussian source-bin count");
  check(local_acr::kGaussianPenaltiesQ16[0].size() == 255, "Gaussian target-bin count");
  check(local_acr::kGaussianPenaltiesQ16[6][6] == 0, "Gaussian self penalty is zero");
  check(local_acr::kGaussianPenaltiesQ16[0][254] == -16 * 65536,
        "Gaussian penalties clamp at -16");

  const double sigma = 3.0 * std::sqrt(17.0 + 3.0);
  const double value = -0.5 * ((20.0 - 17.0) / sigma) * ((20.0 - 17.0) / sigma);
  check(local_acr::kGaussianPenaltiesQ16[16][19] == reference_round_q16(value),
        "Gaussian near-bin value is pinned");
}

void warmup_collects_thresholds_without_emitting() {
  PeakSelector selector;
  CapturingPeakSink sink;
  feed_warmup(selector, sink);

  check(sink.peaks.empty(), "warm-up emits no peaks");
  check(selector.processed_frames() == PeakSelector::kWarmupFrames, "warm-up frame count");
  check(selector.provisional_count() == 0, "warm-up stores no provisional peaks");
}

void plateau_uses_lowest_bin_and_confirms_after_22_hops() {
  PeakSelector selector;
  CapturingPeakSink sink;
  feed_warmup(selector, sink);

  DecisionSpectrum plateau = quiet_frame(10);
  set_bin(plateau, 40, 20 * 65536);
  set_bin(plateau, 41, 20 * 65536);
  check(selector.process(plateau, sink).ok(), "plateau frame succeeds");
  check(sink.peaks.empty(), "new peak is provisional");

  for (std::uint64_t time = 11; time <= 31; ++time) {
    check(selector.process(quiet_frame(time), sink).ok(), "look-ahead filler succeeds");
    check(sink.peaks.empty(), "not confirmed before exact age 22");
  }

  check(selector.process(quiet_frame(32), sink).ok(), "exact confirmation frame succeeds");
  check(sink.peaks.size() == 1, "one plateau peak confirmed");
  if (!sink.peaks.empty()) {
    check(sink.peaks[0].time_frame == 10, "confirmed peak time");
    check(sink.peaks[0].bin == 40, "lowest plateau bin is selected");
    check(sink.peaks[0].value_q16 == 20 * 65536, "confirmed peak value");
  }
}

void top_five_order_is_descending_value_then_ascending_bin() {
  PeakSelector selector;
  CapturingPeakSink sink;
  feed_warmup(selector, sink);

  DecisionSpectrum spectrum = quiet_frame(10);
  set_bin(spectrum, 10, 8 * 65536);
  set_bin(spectrum, 20, 11 * 65536);
  set_bin(spectrum, 30, 9 * 65536);
  set_bin(spectrum, 40, 11 * 65536);
  set_bin(spectrum, 50, 7 * 65536);
  set_bin(spectrum, 60, 10 * 65536);
  check(selector.process(spectrum, sink).ok(), "candidate frame succeeds");

  for (std::uint64_t time = 11; time <= 32; ++time) {
    check(selector.process(quiet_frame(time), sink).ok(), "confirmation filler succeeds");
  }

  const std::array<std::uint8_t, 5> expected_bins{10, 20, 30, 40, 60};
  check(sink.peaks.size() == expected_bins.size(), "top five peaks confirmed");
  for (std::size_t index = 0; index < sink.peaks.size() && index < expected_bins.size(); ++index) {
    check(sink.peaks[index].bin == expected_bins[index], "top-five confirmation order");
  }
}

void newer_peak_suppresses_older_peak() {
  PeakSelector selector;
  CapturingPeakSink sink;
  feed_warmup(selector, sink);

  DecisionSpectrum older = quiet_frame(10);
  set_bin(older, 80, 20 * 65536);
  check(selector.process(older, sink).ok(), "older peak frame succeeds");

  DecisionSpectrum newer = quiet_frame(11);
  set_bin(newer, 80, 21 * 65536);
  check(selector.process(newer, sink).ok(), "newer peak frame succeeds");

  for (std::uint64_t time = 12; time <= 33; ++time) {
    check(selector.process(quiet_frame(time), sink).ok(), "suppression filler succeeds");
  }

  check(sink.peaks.size() == 1, "newer equal peak suppresses older");
  if (!sink.peaks.empty()) {
    check(sink.peaks[0].time_frame == 11, "newer peak survives");
  }
}

void finite_finish_discards_unconfirmed_tail() {
  std::vector<DecisionSpectrum> stream;
  for (std::uint64_t time = 0; time <= 17; ++time) {
    DecisionSpectrum spectrum = time < 10 ? frame(time) : quiet_frame(time);
    if (time == 10) {
      set_bin(spectrum, 90, 20 * 65536);
    }
    stream.push_back(spectrum);
  }

  PeakSelector finite_selector;
  CapturingPeakSink finite_sink;
  for (const DecisionSpectrum& spectrum : stream) {
    check(finite_selector.process(spectrum, finite_sink).ok(), "finite stream frame succeeds");
  }
  check(finite_selector.finish_finite(finite_sink).ok(), "finite finish succeeds");
  check(finite_sink.peaks.empty(), "finite finish discards young provisional peaks");
  check(finite_selector.provisional_count() == 0, "finite finish clears provisional storage");

  PeakSelector live_selector;
  CapturingPeakSink live_sink;
  for (const DecisionSpectrum& spectrum : stream) {
    check(live_selector.process(spectrum, live_sink).ok(), "live stream frame succeeds");
  }
  check(live_sink.peaks == finite_sink.peaks, "finite and live stopped at same boundary match");
}

void arbitrary_chunking_is_equivalent_and_storage_is_bounded() {
  std::vector<DecisionSpectrum> stream;
  for (std::uint64_t time = 0; time < 80; ++time) {
    DecisionSpectrum spectrum = time < 10 ? frame(time) : quiet_frame(time);
    if (time >= 10) {
      set_bin(spectrum, static_cast<std::uint8_t>(20 + (time % 31)), 30 * 65536);
      set_bin(spectrum, static_cast<std::uint8_t>(80 + (time % 29)), 29 * 65536);
      set_bin(spectrum, static_cast<std::uint8_t>(140 + (time % 23)), 28 * 65536);
    }
    stream.push_back(spectrum);
  }

  PeakSelector one_batch;
  CapturingPeakSink one_batch_sink;
  for (const DecisionSpectrum& spectrum : stream) {
    check(one_batch.process(spectrum, one_batch_sink).ok(), "one-batch stream succeeds");
    check(one_batch.provisional_count() <= PeakSelector::kMaxProvisionalPeaks,
          "one-batch provisional storage is bounded");
  }

  PeakSelector chunked;
  CapturingPeakSink chunked_sink;
  std::size_t offset = 0;
  const std::array<std::size_t, 7> chunks{1, 7, 3, 19, 2, 11, 37};
  for (const std::size_t chunk : chunks) {
    for (std::size_t i = 0; i < chunk && offset < stream.size(); ++i) {
      check(chunked.process(stream[offset], chunked_sink).ok(), "chunked stream succeeds");
      check(chunked.provisional_count() <= PeakSelector::kMaxProvisionalPeaks,
            "chunked provisional storage is bounded");
      ++offset;
    }
  }
  while (offset < stream.size()) {
    check(chunked.process(stream[offset], chunked_sink).ok(), "chunked tail succeeds");
    ++offset;
  }

  check(chunked_sink.peaks == one_batch_sink.peaks, "chunked and one-batch peaks match");
}

}  // namespace

int main() {
  generated_constants_are_checked();
  warmup_collects_thresholds_without_emitting();
  plateau_uses_lowest_bin_and_confirms_after_22_hops();
  top_five_order_is_descending_value_then_ascending_bin();
  newer_peak_suppresses_older_peak();
  finite_finish_discards_unconfirmed_tail();
  arbitrary_chunking_is_equivalent_and_storage_is_bounded();
  return failures == 0 ? 0 : 1;
}
