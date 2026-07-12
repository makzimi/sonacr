enable_language(OBJCXX)

add_library(local_acr_ios_bridge STATIC)

target_sources(
  local_acr_ios_bridge
  PRIVATE
    src/local_acr_ios_bridge.mm
)

target_include_directories(
  local_acr_ios_bridge
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
  PRIVATE
    "${PROJECT_SOURCE_DIR}/native/core/include"
)

target_link_libraries(
  local_acr_ios_bridge
  PUBLIC
    local_acr_core
    "-framework AVFoundation"
    "-framework Foundation"
)

target_compile_features(local_acr_ios_bridge PUBLIC cxx_std_20)
target_compile_options(local_acr_ios_bridge PRIVATE -fobjc-arc -fblocks)

set_target_properties(
  local_acr_ios_bridge
  PROPERTIES
    OBJCXX_EXTENSIONS OFF
    OBJCXX_STANDARD 20
    OBJCXX_STANDARD_REQUIRED ON
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
)

lacr_apply_warnings(local_acr_ios_bridge)
lacr_apply_strict_fp(local_acr_ios_bridge)
