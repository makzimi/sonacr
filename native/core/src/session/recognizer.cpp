#include "session/recognizer.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>

namespace local_acr {

namespace {

constexpr std::uint32_t kAnalysisSampleRate = 11025;
constexpr std::uint32_t kAnalysisHopSize = 128;
constexpr std::uint32_t kFirstEvaluationFrame =
    ((2U * kAnalysisSampleRate) + kAnalysisHopSize - 1U) / kAnalysisHopSize;
constexpr std::uint32_t kEvaluationCadenceFrames = 22;
constexpr std::uint32_t kRollingWindowFrames =
    ((4U * kAnalysisSampleRate) + kAnalysisHopSize - 1U) / kAnalysisHopSize;
constexpr std::uint32_t kMaxQueryLandmarks = 512;
constexpr std::size_t kMaxPendingEvents = 32;

RecognizerError status_to_error(const Status status) noexcept {
  if (status.code() == StatusCode::AudioDiscontinuity) {
    return RecognizerError::AudioDiscontinuity;
  }
  return RecognizerError::NativeEngineFailure;
}

std::vector<TriggerCueDuration> conservative_durations_for(
    const std::vector<AlignedCandidate>& candidates) {
  std::vector<TriggerCueDuration> durations;
  durations.reserve(candidates.size());
  for (const AlignedCandidate& candidate : candidates) {
    const auto found = std::find_if(durations.begin(), durations.end(), [&](const TriggerCueDuration& row) {
      return row.trigger_id == candidate.trigger_id;
    });
    if (found == durations.end()) {
      durations.push_back(TriggerCueDuration{
          .trigger_id = candidate.trigger_id,
          .duration_frames = 10000000U,
      });
    }
  }
  return durations;
}

}  // namespace

Recognizer::Recognizer(const std::uint32_t input_sample_rate) noexcept
    : fingerprinter_(input_sample_rate) {}

Status Recognizer::prepare(DatabaseReader& reader) noexcept {
  if (reader.sqlite_handle_for_matcher() == nullptr) {
    return Status::invalid_argument();
  }
  reader_ = &reader;
  return Status::ok_status();
}

Status Recognizer::start_session(const std::uint64_t generation) noexcept {
  if (!fingerprinter_.valid()) {
    return Status::native_engine_failure();
  }
  generation_ = generation;
  active_ = true;
  events_.clear();
  reset_session_state();
  return Status::ok_status();
}

Status Recognizer::push_pcm(const PcmView& pcm) noexcept {
  if (!active_) {
    return Status::invalid_state();
  }
  newest_source_frame_ = pcm.first_source_frame + pcm.frames;
  const Status status = fingerprinter_.push(pcm, *this);
  if (!status.ok()) {
    emit_error(status_to_error(status));
    reset_session_state();
    active_ = false;
  }
  return status;
}

Status Recognizer::stop_session() noexcept {
  active_ = false;
  events_.clear();
  reset_session_state();
  return Status::ok_status();
}

std::optional<RecognizerEvent> Recognizer::poll_event() {
  if (events_.empty()) {
    return std::nullopt;
  }
  RecognizerEvent event = std::move(events_.front());
  events_.erase(events_.begin());
  return event;
}

bool Recognizer::active() const noexcept {
  return active_;
}

Status Recognizer::inject_query_landmarks_for_test(const std::uint32_t count,
                                                   const std::uint32_t newest_time_frame) noexcept {
  if (!active_) {
    return Status::invalid_state();
  }
  query_.clear();
  query_.reserve(count);
  for (std::uint32_t index = 0; index < count; ++index) {
    query_.push_back(QueryLandmark{
        .query_id = index,
        .hash = index & 0x00FFFFFFU,
        .time_frame = newest_time_frame,
    });
  }
  newest_time_frame_ = newest_time_frame;
  next_evaluation_frame_ = newest_time_frame;
  return evaluate_until_current();
}

Status Recognizer::consume(std::span<const Landmark> landmarks) noexcept {
  for (const Landmark& landmark : landmarks) {
    const Status status = append_landmark(landmark);
    if (!status.ok()) {
      return status;
    }
  }
  return Status::ok_status();
}

Status Recognizer::append_landmark(const Landmark& landmark) noexcept {
  query_.push_back(QueryLandmark{
      .query_id = next_query_id_++,
      .hash = landmark.hash,
      .time_frame = landmark.anchor_time_frame,
  });
  newest_time_frame_ = landmark.anchor_time_frame;
  expire_query();
  return evaluate_until_current();
}

Status Recognizer::evaluate_until_current() noexcept {
  if (!active_) {
    return Status::ok_status();
  }
  while (newest_time_frame_ >= next_evaluation_frame_) {
    if (query_.size() > kMaxQueryLandmarks) {
      query_.erase(query_.begin(),
                   query_.end() - static_cast<std::ptrdiff_t>(kMaxQueryLandmarks));
    }
    if (next_evaluation_frame_ >= kFirstEvaluationFrame) {
      const Status status = evaluate_once();
      if (!status.ok()) {
        emit_error(status_to_error(status));
        reset_session_state();
        active_ = false;
        return status;
      }
    }
    next_evaluation_frame_ += kEvaluationCadenceFrames;
  }
  return Status::ok_status();
}

Status Recognizer::evaluate_once() noexcept {
  if (reader_ == nullptr || query_.empty()) {
    return Status::ok_status();
  }
  CandidateLookupResult lookup_result = lookup_.lookup(*reader_, query_);
  if (!lookup_result.status.ok()) {
    if (lookup_result.status.code() == StatusCode::ResourceLimitExceeded) {
      return Status::ok_status();
    }
    return lookup_result.status;
  }

  MatcherResult matcher_result = matcher_.evaluate(MatcherEvaluation{
      .candidates = lookup_result.candidates,
      .previous_winner = previous_winner_,
      .unique_query_landmark_count = static_cast<std::uint32_t>(query_.size()),
      .newest_query_time_frame = newest_time_frame_,
      .newest_source_frame = newest_source_frame_,
      .trigger_durations = conservative_durations_for(lookup_result.candidates),
  });
  if (!matcher_result.diagnostics.winner_trigger_id.empty()) {
    previous_winner_ = PreviousWinner{
        .trigger_id = matcher_result.diagnostics.winner_trigger_id,
        .center_bucket = matcher_result.diagnostics.winner_center_bucket,
    };
  }
  if (matcher_result.recognized.has_value()) {
    emit_recognition(*matcher_result.recognized);
  }
  return Status::ok_status();
}

void Recognizer::expire_query() noexcept {
  if (newest_time_frame_ <= kRollingWindowFrames) {
    return;
  }
  const std::uint32_t oldest = newest_time_frame_ - kRollingWindowFrames;
  query_.erase(std::remove_if(query_.begin(), query_.end(), [&](const QueryLandmark& landmark) {
                 return landmark.time_frame < oldest;
               }),
               query_.end());
}

void Recognizer::reset_session_state() noexcept {
  fingerprinter_.reset();
  query_.clear();
  previous_winner_.reset();
  next_query_id_ = 0;
  newest_time_frame_ = 0;
  newest_source_frame_ = 0;
  next_evaluation_frame_ = kFirstEvaluationFrame;
}

void Recognizer::emit_error(const RecognizerError error) noexcept {
  if (events_.size() >= kMaxPendingEvents) {
    events_.erase(events_.begin());
  }
  events_.push_back(RecognizerEvent{
      .type = RecognizerEventType::SessionError,
      .generation = generation_,
      .recognition = {},
      .error = error,
      .newest_source_frame = newest_source_frame_,
  });
}

void Recognizer::emit_recognition(const NativeRecognitionResult& recognition) noexcept {
  if (events_.size() >= kMaxPendingEvents) {
    events_.erase(events_.begin());
  }
  events_.push_back(RecognizerEvent{
      .type = RecognizerEventType::Recognition,
      .generation = generation_,
      .recognition = recognition,
      .error = RecognizerError::None,
      .newest_source_frame = recognition.newest_source_frame,
  });
}

}  // namespace local_acr
