#include "fingerprint/fingerprinter.hpp"

namespace local_acr {

Fingerprinter::Fingerprinter(const std::uint32_t input_sample_rate) noexcept
    : input_sample_rate_(input_sample_rate), pcm_ingress_(mono_scratch_) {
  resampler_.emplace(input_sample_rate_, resampler_scratch_);
}

bool Fingerprinter::valid() const noexcept {
  return resampler_.has_value() && resampler_->valid();
}

Status Fingerprinter::push(const PcmView& pcm, LandmarkSink& sink) noexcept {
  if (!valid()) {
    return Status::native_engine_failure();
  }
  active_sink_ = &sink;
  const Status status = pcm_ingress_.push(pcm, *this);
  active_sink_ = nullptr;
  return status;
}

Status Fingerprinter::finish_finite(LandmarkSink& sink) noexcept {
  if (!valid()) {
    return Status::native_engine_failure();
  }
  active_sink_ = &sink;
  Status status = resampler_->finish(*this);
  if (status.ok()) {
    status = frame_stream_.finish();
  }
  if (status.ok()) {
    status = peak_selector_.finish_finite(*this);
  }
  if (status.ok()) {
    status = landmark_builder_.finish_finite(sink);
  }
  active_sink_ = nullptr;
  return status;
}

void Fingerprinter::reset() noexcept {
  pcm_ingress_.reset();
  resampler_.reset();
  resampler_.emplace(input_sample_rate_, resampler_scratch_);
  frame_stream_.reset();
  spectrum_.reset();
  peak_selector_.reset();
  landmark_builder_.reset();
  active_sink_ = nullptr;
}

Status Fingerprinter::consume(const MonoPcmView pcm) noexcept {
  if (pcm.sample_rate != input_sample_rate_) {
    return Status::invalid_argument();
  }
  return resampler_->push(pcm.samples, *this);
}

Status Fingerprinter::consume(std::span<const float> samples) noexcept {
  if (samples.empty()) {
    return Status::ok_status();
  }
  return frame_stream_.push(samples, *this);
}

Status Fingerprinter::consume(const AnalysisFrame& frame) noexcept {
  if (active_sink_ == nullptr) {
    return Status::invalid_state();
  }
  DecisionSpectrum spectrum{};
  const Status spectrum_status = spectrum_.process(frame, spectrum);
  if (!spectrum_status.ok()) {
    return spectrum_status;
  }
  return peak_selector_.process(spectrum, *this);
}

Status Fingerprinter::consume(std::span<const ConfirmedPeak> peaks) noexcept {
  if (active_sink_ == nullptr) {
    return Status::invalid_state();
  }
  return landmark_builder_.process(peaks, *active_sink_);
}

}  // namespace local_acr
