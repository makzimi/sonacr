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
