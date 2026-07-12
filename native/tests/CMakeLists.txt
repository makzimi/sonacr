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

add_executable(fingerprinter_test integration/fingerprinter_test.cpp)
target_link_libraries(fingerprinter_test PRIVATE local_acr_core)
target_include_directories(fingerprinter_test PRIVATE "${PROJECT_SOURCE_DIR}/native/core/src")
target_compile_features(fingerprinter_test PRIVATE cxx_std_20)
lacr_apply_warnings(fingerprinter_test)
lacr_apply_sanitizers(fingerprinter_test)
lacr_apply_strict_fp(fingerprinter_test)

add_test(NAME fingerprinter_test COMMAND fingerprinter_test)
set_tests_properties(fingerprinter_test PROPERTIES LABELS native-integration)

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
