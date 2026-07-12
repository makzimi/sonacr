add_library(local_acr_qualification_lib STATIC)

target_sources(
  local_acr_qualification_lib
  PRIVATE
    src/qualification_runner.cpp
)

target_include_directories(
  local_acr_qualification_lib
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${PROJECT_SOURCE_DIR}/native/core/src"
)

target_compile_features(local_acr_qualification_lib PUBLIC cxx_std_20)
target_link_libraries(local_acr_qualification_lib PUBLIC local_acr_core)

lacr_apply_warnings(local_acr_qualification_lib)
lacr_apply_sanitizers(local_acr_qualification_lib)
lacr_apply_strict_fp(local_acr_qualification_lib)

add_executable(local_acr_qualification src/main.cpp)
target_link_libraries(local_acr_qualification PRIVATE local_acr_qualification_lib)
target_compile_features(local_acr_qualification PRIVATE cxx_std_20)
lacr_apply_warnings(local_acr_qualification)
lacr_apply_sanitizers(local_acr_qualification)
lacr_apply_strict_fp(local_acr_qualification)

if(LACR_BUILD_TESTS)
  add_executable(qualification_runner_test tests/qualification_runner_test.cpp)
  target_link_libraries(qualification_runner_test PRIVATE local_acr_qualification_lib)
  target_compile_features(qualification_runner_test PRIVATE cxx_std_20)
  lacr_apply_warnings(qualification_runner_test)
  lacr_apply_sanitizers(qualification_runner_test)
  lacr_apply_strict_fp(qualification_runner_test)

  add_test(NAME qualification_runner_test COMMAND qualification_runner_test)
  set_tests_properties(qualification_runner_test PROPERTIES LABELS qualification)
endif()
