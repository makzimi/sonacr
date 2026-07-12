add_executable(version_test unit/version_test.cpp)

target_link_libraries(version_test PRIVATE local_acr_core)
target_compile_features(version_test PRIVATE cxx_std_20)

lacr_apply_warnings(version_test)
lacr_apply_sanitizers(version_test)
lacr_apply_strict_fp(version_test)

add_test(NAME version_test COMMAND version_test)
set_tests_properties(version_test PROPERTIES LABELS native-unit)

add_executable(pcm_ingress_test unit/pcm_ingress_test.cpp)
target_link_libraries(pcm_ingress_test PRIVATE local_acr_core)
target_include_directories(pcm_ingress_test PRIVATE "${PROJECT_SOURCE_DIR}/native/core/src")
target_compile_features(pcm_ingress_test PRIVATE cxx_std_20)
lacr_apply_warnings(pcm_ingress_test)
lacr_apply_sanitizers(pcm_ingress_test)
lacr_apply_strict_fp(pcm_ingress_test)

add_test(NAME pcm_ingress_test COMMAND pcm_ingress_test)
set_tests_properties(pcm_ingress_test PROPERTIES LABELS native-unit)

add_executable(resampler_test unit/resampler_test.cpp)
target_link_libraries(resampler_test PRIVATE local_acr_core)
target_include_directories(resampler_test PRIVATE "${PROJECT_SOURCE_DIR}/native/core/src")
target_compile_features(resampler_test PRIVATE cxx_std_20)
lacr_apply_warnings(resampler_test)
lacr_apply_sanitizers(resampler_test)
lacr_apply_strict_fp(resampler_test)

add_test(NAME resampler_test COMMAND resampler_test)
set_tests_properties(resampler_test PROPERTIES LABELS native-unit)

add_executable(frame_stream_test unit/frame_stream_test.cpp)
target_link_libraries(frame_stream_test PRIVATE local_acr_core)
target_include_directories(frame_stream_test PRIVATE "${PROJECT_SOURCE_DIR}/native/core/src")
target_compile_features(frame_stream_test PRIVATE cxx_std_20)
lacr_apply_warnings(frame_stream_test)
lacr_apply_sanitizers(frame_stream_test)
lacr_apply_strict_fp(frame_stream_test)

add_test(NAME frame_stream_test COMMAND frame_stream_test)
set_tests_properties(frame_stream_test PROPERTIES LABELS native-unit)

add_executable(log_q16_test unit/log_q16_test.cpp)
target_link_libraries(log_q16_test PRIVATE local_acr_core)
target_include_directories(log_q16_test PRIVATE "${PROJECT_SOURCE_DIR}/native/core/src")
target_compile_features(log_q16_test PRIVATE cxx_std_20)
lacr_apply_warnings(log_q16_test)
lacr_apply_sanitizers(log_q16_test)
lacr_apply_strict_fp(log_q16_test)

add_test(NAME log_q16_test COMMAND log_q16_test)
set_tests_properties(log_q16_test PROPERTIES LABELS native-unit)

add_executable(temporal_filter_test unit/temporal_filter_test.cpp)
target_link_libraries(temporal_filter_test PRIVATE local_acr_core)
target_include_directories(temporal_filter_test PRIVATE "${PROJECT_SOURCE_DIR}/native/core/src")
target_compile_features(temporal_filter_test PRIVATE cxx_std_20)
lacr_apply_warnings(temporal_filter_test)
lacr_apply_sanitizers(temporal_filter_test)
lacr_apply_strict_fp(temporal_filter_test)

add_test(NAME temporal_filter_test COMMAND temporal_filter_test)
set_tests_properties(temporal_filter_test PROPERTIES LABELS native-unit)

add_executable(spectrum_test unit/spectrum_test.cpp)
target_link_libraries(spectrum_test PRIVATE local_acr_core)
target_include_directories(spectrum_test PRIVATE "${PROJECT_SOURCE_DIR}/native/core/src")
target_compile_features(spectrum_test PRIVATE cxx_std_20)
lacr_apply_warnings(spectrum_test)
lacr_apply_sanitizers(spectrum_test)
lacr_apply_strict_fp(spectrum_test)

add_test(NAME spectrum_test COMMAND spectrum_test)
set_tests_properties(spectrum_test PROPERTIES LABELS native-unit)

add_executable(peak_selector_test unit/peak_selector_test.cpp)
target_link_libraries(peak_selector_test PRIVATE local_acr_core)
target_include_directories(peak_selector_test PRIVATE "${PROJECT_SOURCE_DIR}/native/core/src")
target_compile_features(peak_selector_test PRIVATE cxx_std_20)
lacr_apply_warnings(peak_selector_test)
lacr_apply_sanitizers(peak_selector_test)
lacr_apply_strict_fp(peak_selector_test)

add_test(NAME peak_selector_test COMMAND peak_selector_test)
set_tests_properties(peak_selector_test PROPERTIES LABELS native-unit)

add_executable(landmark_builder_test unit/landmark_builder_test.cpp)
target_link_libraries(landmark_builder_test PRIVATE local_acr_core)
target_include_directories(landmark_builder_test PRIVATE "${PROJECT_SOURCE_DIR}/native/core/src")
target_compile_features(landmark_builder_test PRIVATE cxx_std_20)
lacr_apply_warnings(landmark_builder_test)
lacr_apply_sanitizers(landmark_builder_test)
lacr_apply_strict_fp(landmark_builder_test)

add_test(NAME landmark_builder_test COMMAND landmark_builder_test)
set_tests_properties(landmark_builder_test PROPERTIES LABELS native-unit)

add_executable(offset_accumulator_test unit/offset_accumulator_test.cpp)
target_link_libraries(offset_accumulator_test PRIVATE local_acr_core)
target_include_directories(offset_accumulator_test PRIVATE "${PROJECT_SOURCE_DIR}/native/core/src")
target_compile_features(offset_accumulator_test PRIVATE cxx_std_20)
lacr_apply_warnings(offset_accumulator_test)
lacr_apply_sanitizers(offset_accumulator_test)
lacr_apply_strict_fp(offset_accumulator_test)

add_test(NAME offset_accumulator_test COMMAND offset_accumulator_test)
set_tests_properties(offset_accumulator_test PROPERTIES LABELS native-unit)

add_executable(conservative_matcher_test unit/conservative_matcher_test.cpp)
target_link_libraries(conservative_matcher_test PRIVATE local_acr_core)
target_include_directories(conservative_matcher_test PRIVATE "${PROJECT_SOURCE_DIR}/native/core/src")
target_compile_features(conservative_matcher_test PRIVATE cxx_std_20)
lacr_apply_warnings(conservative_matcher_test)
lacr_apply_sanitizers(conservative_matcher_test)
lacr_apply_strict_fp(conservative_matcher_test)

add_test(NAME conservative_matcher_test COMMAND conservative_matcher_test)
set_tests_properties(conservative_matcher_test PROPERTIES LABELS native-unit)

add_executable(fingerprinter_test integration/fingerprinter_test.cpp)
target_link_libraries(fingerprinter_test PRIVATE local_acr_core)
target_include_directories(fingerprinter_test PRIVATE "${PROJECT_SOURCE_DIR}/native/core/src")
target_compile_features(fingerprinter_test PRIVATE cxx_std_20)
lacr_apply_warnings(fingerprinter_test)
lacr_apply_sanitizers(fingerprinter_test)
lacr_apply_strict_fp(fingerprinter_test)

add_test(NAME fingerprinter_test COMMAND fingerprinter_test)
set_tests_properties(fingerprinter_test PROPERTIES LABELS native-integration)

add_executable(database_test integration/database_test.cpp)
target_link_libraries(database_test PRIVATE local_acr_core)
target_include_directories(database_test PRIVATE "${PROJECT_SOURCE_DIR}/native/core/src")
target_compile_features(database_test PRIVATE cxx_std_20)
lacr_apply_warnings(database_test)
lacr_apply_sanitizers(database_test)
lacr_apply_strict_fp(database_test)

add_test(NAME database_test COMMAND database_test)
set_tests_properties(database_test PROPERTIES LABELS native-integration)

add_executable(candidate_lookup_test integration/candidate_lookup_test.cpp)
target_link_libraries(candidate_lookup_test PRIVATE local_acr_core)
target_include_directories(candidate_lookup_test PRIVATE "${PROJECT_SOURCE_DIR}/native/core/src")
target_compile_features(candidate_lookup_test PRIVATE cxx_std_20)
lacr_apply_warnings(candidate_lookup_test)
lacr_apply_sanitizers(candidate_lookup_test)
lacr_apply_strict_fp(candidate_lookup_test)

add_test(NAME candidate_lookup_test COMMAND candidate_lookup_test)
set_tests_properties(candidate_lookup_test PROPERTIES LABELS native-integration)

add_executable(recognizer_session_test integration/recognizer_session_test.cpp)
target_link_libraries(recognizer_session_test PRIVATE local_acr_core)
target_include_directories(recognizer_session_test PRIVATE "${PROJECT_SOURCE_DIR}/native/core/src")
target_compile_features(recognizer_session_test PRIVATE cxx_std_20)
lacr_apply_warnings(recognizer_session_test)
lacr_apply_sanitizers(recognizer_session_test)
lacr_apply_strict_fp(recognizer_session_test)

add_test(NAME recognizer_session_test COMMAND recognizer_session_test)
set_tests_properties(recognizer_session_test PROPERTIES LABELS native-integration)

add_executable(parity_goldens_test integration/parity_goldens_test.cpp)
target_link_libraries(parity_goldens_test PRIVATE local_acr_core)
target_include_directories(parity_goldens_test PRIVATE "${PROJECT_SOURCE_DIR}/native/core/src")
target_compile_features(parity_goldens_test PRIVATE cxx_std_20)
lacr_apply_warnings(parity_goldens_test)
lacr_apply_sanitizers(parity_goldens_test)
lacr_apply_strict_fp(parity_goldens_test)
target_compile_definitions(
  parity_goldens_test
  PRIVATE
    LACR_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
)

add_test(NAME parity_goldens_test COMMAND parity_goldens_test)
set_tests_properties(parity_goldens_test PROPERTIES LABELS "native-integration;parity")

add_executable(malformed_input_property_test integration/malformed_input_property_test.cpp)
target_link_libraries(malformed_input_property_test PRIVATE local_acr_core)
target_include_directories(malformed_input_property_test PRIVATE "${PROJECT_SOURCE_DIR}/native/core/src")
target_compile_features(malformed_input_property_test PRIVATE cxx_std_20)
lacr_apply_warnings(malformed_input_property_test)
lacr_apply_sanitizers(malformed_input_property_test)
lacr_apply_strict_fp(malformed_input_property_test)

add_test(NAME malformed_input_property_test COMMAND malformed_input_property_test)
set_tests_properties(malformed_input_property_test PROPERTIES LABELS "native-integration;safety")

add_executable(nominal_soak_test integration/nominal_soak_test.cpp)
target_link_libraries(nominal_soak_test PRIVATE local_acr_core)
target_include_directories(nominal_soak_test PRIVATE "${PROJECT_SOURCE_DIR}/native/core/src")
target_compile_features(nominal_soak_test PRIVATE cxx_std_20)
lacr_apply_warnings(nominal_soak_test)
lacr_apply_sanitizers(nominal_soak_test)
lacr_apply_strict_fp(nominal_soak_test)

add_test(NAME nominal_soak_test COMMAND nominal_soak_test)
set_tests_properties(nominal_soak_test PROPERTIES LABELS "native-integration;soak")

add_executable(spsc_pcm_queue_test unit/spsc_pcm_queue_test.cpp)
target_link_libraries(spsc_pcm_queue_test PRIVATE local_acr_core)
target_include_directories(spsc_pcm_queue_test PRIVATE "${PROJECT_SOURCE_DIR}/native/core/src")
target_compile_features(spsc_pcm_queue_test PRIVATE cxx_std_20)
lacr_apply_warnings(spsc_pcm_queue_test)
lacr_apply_sanitizers(spsc_pcm_queue_test)
lacr_apply_strict_fp(spsc_pcm_queue_test)

add_test(NAME spsc_pcm_queue_test COMMAND spsc_pcm_queue_test)
set_tests_properties(spsc_pcm_queue_test PROPERTIES LABELS native-unit)

add_executable(event_codec_test unit/event_codec_test.cpp)
target_link_libraries(event_codec_test PRIVATE local_acr_core)
target_include_directories(event_codec_test PRIVATE "${PROJECT_SOURCE_DIR}/native/core/src")
target_compile_features(event_codec_test PRIVATE cxx_std_20)
lacr_apply_warnings(event_codec_test)
lacr_apply_sanitizers(event_codec_test)
lacr_apply_strict_fp(event_codec_test)

add_test(NAME event_codec_test COMMAND event_codec_test)
set_tests_properties(event_codec_test PROPERTIES LABELS native-unit)

add_executable(c_api_test integration/c_api_test.c)
target_link_libraries(c_api_test PRIVATE local_acr_core)
target_compile_features(c_api_test PRIVATE c_std_11)
lacr_apply_sanitizers(c_api_test)

add_test(NAME c_api_test COMMAND c_api_test)
set_tests_properties(c_api_test PROPERTIES LABELS native-integration)

add_executable(c_api_concurrency_test integration/c_api_concurrency_test.cpp)
target_link_libraries(c_api_concurrency_test PRIVATE local_acr_core)
target_compile_features(c_api_concurrency_test PRIVATE cxx_std_20)
lacr_apply_warnings(c_api_concurrency_test)
lacr_apply_sanitizers(c_api_concurrency_test)
lacr_apply_strict_fp(c_api_concurrency_test)

add_test(NAME c_api_concurrency_test COMMAND c_api_concurrency_test)
set_tests_properties(c_api_concurrency_test PROPERTIES LABELS native-integration)

if(APPLE)
  find_library(LACR_SYSTEM_SQLITE_LIBRARY sqlite3)
  if(LACR_SYSTEM_SQLITE_LIBRARY)
    add_executable(sqlite_symbol_isolation_test integration/sqlite_symbol_isolation_test.cpp)
    target_link_libraries(
      sqlite_symbol_isolation_test
      PRIVATE
        local_acr_core
        "${LACR_SYSTEM_SQLITE_LIBRARY}"
    )
    target_include_directories(sqlite_symbol_isolation_test PRIVATE "${PROJECT_SOURCE_DIR}/native/core/src")
    target_compile_features(sqlite_symbol_isolation_test PRIVATE cxx_std_20)
    lacr_apply_warnings(sqlite_symbol_isolation_test)
    lacr_apply_sanitizers(sqlite_symbol_isolation_test)
    lacr_apply_strict_fp(sqlite_symbol_isolation_test)

    add_test(NAME sqlite_symbol_isolation_test COMMAND sqlite_symbol_isolation_test)
    set_tests_properties(sqlite_symbol_isolation_test PROPERTIES LABELS native-integration)
  endif()
endif()

add_test(
  NAME dependency_digest_test
  COMMAND
    "${CMAKE_COMMAND}"
    "-DLACR_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    "-DLACR_TEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/dependency-digest-test"
    -P
    "${PROJECT_SOURCE_DIR}/cmake/tests/dependency_digest_test.cmake"
)
set_tests_properties(dependency_digest_test PROPERTIES LABELS native-build)

add_test(
  NAME dependency_lock_test
  COMMAND
    "${CMAKE_COMMAND}"
    "-DLACR_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    -P
    "${PROJECT_SOURCE_DIR}/cmake/tests/dependency_lock_test.cmake"
)
set_tests_properties(dependency_lock_test PROPERTIES LABELS native-build)
