add_library(local_acr_core STATIC)

target_sources(
  local_acr_core
  PRIVATE
    src/audio/pcm_ingress.cpp
    src/support/version.cpp
)

target_include_directories(
  local_acr_core
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
  PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_compile_features(local_acr_core PUBLIC cxx_std_20)

set_target_properties(
  local_acr_core
  PROPERTIES
    CXX_EXTENSIONS OFF
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
)

lacr_apply_warnings(local_acr_core)
lacr_apply_sanitizers(local_acr_core)
lacr_apply_strict_fp(local_acr_core)
