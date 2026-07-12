add_library(local_acr_cli_lib STATIC)

target_sources(
  local_acr_cli_lib
  PRIVATE
    src/audio_decoder.cpp
    src/manifest.cpp
)

target_include_directories(
  local_acr_cli_lib
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${PROJECT_SOURCE_DIR}/native/core/src"
)

target_compile_features(local_acr_cli_lib PUBLIC cxx_std_20)
target_link_libraries(local_acr_cli_lib PUBLIC local_acr_core)

lacr_apply_warnings(local_acr_cli_lib)
lacr_apply_sanitizers(local_acr_cli_lib)
lacr_apply_strict_fp(local_acr_cli_lib)

add_executable(local_acr_db src/main.cpp)
target_link_libraries(local_acr_db PRIVATE local_acr_cli_lib)
target_compile_features(local_acr_db PRIVATE cxx_std_20)
lacr_apply_warnings(local_acr_db)
lacr_apply_sanitizers(local_acr_db)
lacr_apply_strict_fp(local_acr_db)

if(LACR_BUILD_TESTS)
  add_executable(manifest_test tests/manifest_test.cpp)
  target_link_libraries(manifest_test PRIVATE local_acr_cli_lib)
  target_compile_features(manifest_test PRIVATE cxx_std_20)
  lacr_apply_warnings(manifest_test)
  lacr_apply_sanitizers(manifest_test)
  lacr_apply_strict_fp(manifest_test)

  add_test(NAME manifest_test COMMAND manifest_test)
  set_tests_properties(manifest_test PROPERTIES LABELS cli-unit)

  add_executable(audio_decoder_test tests/audio_decoder_test.cpp)
  target_link_libraries(audio_decoder_test PRIVATE local_acr_cli_lib)
  target_compile_features(audio_decoder_test PRIVATE cxx_std_20)
  lacr_apply_warnings(audio_decoder_test)
  lacr_apply_sanitizers(audio_decoder_test)
  lacr_apply_strict_fp(audio_decoder_test)

  add_test(NAME audio_decoder_test COMMAND audio_decoder_test)
  set_tests_properties(audio_decoder_test PROPERTIES LABELS cli-unit)
endif()
