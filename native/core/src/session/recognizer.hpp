#ifndef LOCAL_ACR_SESSION_RECOGNIZER_HPP
#define LOCAL_ACR_SESSION_RECOGNIZER_HPP

#include "audio/pcm_view.hpp"
#include "database/database_reader.hpp"
#include "fingerprint/fingerprinter.hpp"
#include "matcher/candidate_lookup.hpp"
#include "matcher/conservative_matcher.hpp"
#include "matcher/query_landmark.hpp"
#include "session/event.hpp"
#include "support/status.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace local_acr {

class Recognizer final : private LandmarkSink {
 public:
  explicit Recognizer(std::uint32_t input_sample_rate) noexcept;
  ~Recognizer() override = default;

  Recognizer(const Recognizer&) = delete;
  Recognizer& operator=(const Recognizer&) = delete;

  [[nodiscard]] Status prepare(DatabaseReader& reader) noexcept;
  [[nodiscard]] Status start_session(std::uint64_t generation) noexcept;
  [[nodiscard]] Status push_pcm(const PcmView& pcm) noexcept;
  [[nodiscard]] Status stop_session() noexcept;
  [[nodiscard]] std::optional<RecognizerEvent> poll_event();
  [[nodiscard]] bool active() const noexcept;

  [[nodiscard]] Status inject_query_landmarks_for_test(std::uint32_t count,
                                                       std::uint32_t newest_time_frame) noexcept;

 private:
  Status consume(std::span<const Landmark> landmarks) noexcept override;
  [[nodiscard]] Status append_landmark(const Landmark& landmark) noexcept;
  [[nodiscard]] Status evaluate_until_current() noexcept;
  [[nodiscard]] Status evaluate_once() noexcept;
  void expire_query() noexcept;
  void reset_session_state() noexcept;
  void emit_error(RecognizerError error) noexcept;
  void emit_recognition(const NativeRecognitionResult& recognition) noexcept;

  Fingerprinter fingerprinter_;
  DatabaseReader* reader_ = nullptr;
  CandidateLookup lookup_;
  ConservativeMatcher matcher_;
  std::vector<QueryLandmark> query_;
  std::vector<RecognizerEvent> events_;
  std::optional<PreviousWinner> previous_winner_;
  std::uint64_t generation_ = 0;
  std::uint64_t newest_source_frame_ = 0;
  std::uint32_t next_query_id_ = 0;
  std::uint32_t newest_time_frame_ = 0;
  std::uint32_t next_evaluation_frame_ = 0;
  bool active_ = false;
};

}  // namespace local_acr

#endif
