#include "fingerprint/temporal_filter.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string_view>

namespace {

int failures = 0;

void check(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

void constants_are_pinned() {
  check(local_acr::TemporalFilter::kBinCount == 255, "temporal filter bin count");
  check(local_acr::TemporalFilter::kHpfPoleQ16 == 64225, "HPF pole is pinned");
}

void first_frame_is_zero_and_initializes_previous_input() {
  local_acr::TemporalFilter filter;
  local_acr::LogSpectrum input{};
  input.time_frame = 7;
  input.q16.fill(12345);

  const local_acr::DecisionSpectrum output = filter.process(input);

  check(output.time_frame == 7, "time frame is preserved");
  for (const std::int32_t value : output.q16) {
    check(value == 0, "initial filtered frame is zero");
  }
}

void recurrence_uses_difference_and_previous_output() {
  local_acr::TemporalFilter filter;
  local_acr::LogSpectrum first{};
  first.q16.fill(10 * 65536);
  static_cast<void>(filter.process(first));

  local_acr::LogSpectrum second{};
  second.time_frame = 1;
  second.q16.fill(11 * 65536);
  const local_acr::DecisionSpectrum y1 = filter.process(second);
  check(y1.q16[0] == 65536, "second frame emits input difference");

  local_acr::LogSpectrum third{};
  third.time_frame = 2;
  third.q16.fill(11 * 65536);
  const local_acr::DecisionSpectrum y2 = filter.process(third);
  check(y2.q16[0] == 64225, "third frame decays previous output by pole");
}

void recurrence_saturates_near_int32_boundaries() {
  local_acr::TemporalFilter filter;
  local_acr::LogSpectrum first{};
  first.q16.fill(std::numeric_limits<std::int32_t>::min() + 10);
  static_cast<void>(filter.process(first));

  local_acr::LogSpectrum second{};
  second.time_frame = 1;
  second.q16.fill(std::numeric_limits<std::int32_t>::max() - 10);
  const local_acr::DecisionSpectrum high = filter.process(second);
  check(high.q16[0] == std::numeric_limits<std::int32_t>::max(), "positive delta saturates");

  local_acr::TemporalFilter negative_filter;
  local_acr::LogSpectrum high_first{};
  high_first.q16.fill(std::numeric_limits<std::int32_t>::max() - 10);
  static_cast<void>(negative_filter.process(high_first));

  local_acr::LogSpectrum low_second{};
  low_second.time_frame = 1;
  low_second.q16.fill(std::numeric_limits<std::int32_t>::min() + 10);
  const local_acr::DecisionSpectrum low = negative_filter.process(low_second);
  check(low.q16[0] == std::numeric_limits<std::int32_t>::min(), "negative delta saturates");
}

void reset_restarts_initial_zero_rule() {
  local_acr::TemporalFilter filter;
  local_acr::LogSpectrum first{};
  first.q16.fill(100);
  static_cast<void>(filter.process(first));

  local_acr::LogSpectrum second{};
  second.q16.fill(200);
  static_cast<void>(filter.process(second));

  filter.reset();
  local_acr::LogSpectrum after_reset{};
  after_reset.time_frame = 99;
  after_reset.q16.fill(777);
  const local_acr::DecisionSpectrum output = filter.process(after_reset);

  check(output.time_frame == 99, "reset preserves new time");
  for (const std::int32_t value : output.q16) {
    check(value == 0, "reset makes the next output zero");
  }
}

}  // namespace

int main() {
  constants_are_pinned();
  first_frame_is_zero_and_initializes_previous_input();
  recurrence_uses_difference_and_previous_output();
  recurrence_saturates_near_int32_boundaries();
  reset_restarts_initial_zero_rule();
  return failures == 0 ? 0 : 1;
}
