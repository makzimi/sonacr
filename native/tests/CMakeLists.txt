add_executable(version_test unit/version_test.cpp)

target_link_libraries(version_test PRIVATE local_acr_core)
target_compile_features(version_test PRIVATE cxx_std_20)

lacr_apply_warnings(version_test)
lacr_apply_sanitizers(version_test)
lacr_apply_strict_fp(version_test)

add_test(NAME version_test COMMAND version_test)
set_tests_properties(version_test PROPERTIES LABELS native-unit)

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
