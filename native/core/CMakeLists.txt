add_library(lacr_speexdsp STATIC
  "${PROJECT_SOURCE_DIR}/third_party/speexdsp/src/resample.c"
)
target_include_directories(
  lacr_speexdsp
  PRIVATE
    "${PROJECT_SOURCE_DIR}/third_party/speexdsp/include"
    "${PROJECT_SOURCE_DIR}/third_party/speexdsp/src"
)
target_compile_definitions(
  lacr_speexdsp
  PRIVATE
    EXPORT=
    FLOATING_POINT
    OUTSIDE_SPEEX
    RANDOM_PREFIX=lacr_speex
)
set_target_properties(
  lacr_speexdsp
  PROPERTIES
    C_EXTENSIONS OFF
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    C_VISIBILITY_PRESET hidden
    POSITION_INDEPENDENT_CODE ON
)
lacr_apply_sanitizers(lacr_speexdsp)
lacr_apply_strict_fp(lacr_speexdsp)

if(UNIX AND NOT APPLE)
  target_link_libraries(lacr_speexdsp PUBLIC m)
endif()

add_library(lacr_kissfft STATIC
  "${PROJECT_SOURCE_DIR}/third_party/kissfft/kiss_fft.c"
  "${PROJECT_SOURCE_DIR}/third_party/kissfft/kiss_fftr.c"
)
target_include_directories(lacr_kissfft PUBLIC "${PROJECT_SOURCE_DIR}/third_party/kissfft")
set_target_properties(
  lacr_kissfft
  PROPERTIES
    C_EXTENSIONS OFF
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    C_VISIBILITY_PRESET hidden
    POSITION_INDEPENDENT_CODE ON
)
lacr_apply_sanitizers(lacr_kissfft)
lacr_apply_strict_fp(lacr_kissfft)

if(UNIX AND NOT APPLE)
  target_link_libraries(lacr_kissfft PUBLIC m)
endif()

add_library(lacr_sqlite STATIC
  "${PROJECT_SOURCE_DIR}/third_party/sqlite/sqlite3.c"
)
target_include_directories(lacr_sqlite PUBLIC "${PROJECT_SOURCE_DIR}/third_party/sqlite")
target_compile_definitions(
  lacr_sqlite
  PRIVATE
    SQLITE_THREADSAFE=1
    SQLITE_DEFAULT_FOREIGN_KEYS=1
    SQLITE_DQS=0
    SQLITE_OMIT_DEPRECATED
    SQLITE_OMIT_LOAD_EXTENSION
    SQLITE_OMIT_SHARED_CACHE
    SQLITE_USE_URI=1
)
set_target_properties(
  lacr_sqlite
  PROPERTIES
    C_EXTENSIONS OFF
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    C_VISIBILITY_PRESET hidden
    POSITION_INDEPENDENT_CODE ON
)
lacr_apply_sanitizers(lacr_sqlite)

if(UNIX AND NOT APPLE)
  target_link_libraries(lacr_sqlite PUBLIC m)
endif()

add_library(local_acr_core STATIC)

target_sources(
  local_acr_core
  PRIVATE
    src/audio/frame_stream.cpp
    src/audio/pcm_ingress.cpp
    src/database/database_reader.cpp
    src/database/database_writer.cpp
    src/database/semantic_digest.cpp
    src/fingerprint/log_q16_table.cpp
    src/fingerprint/fingerprinter.cpp
    src/fingerprint/landmark_builder.cpp
    src/fingerprint/peak_selector.cpp
    src/fingerprint/spectrum.cpp
    src/matcher/candidate_lookup.cpp
    src/matcher/conservative_matcher.cpp
    src/matcher/offset_accumulator.cpp
    src/audio/speex_resampler.cpp
    src/support/sha256.cpp
    src/support/version.cpp
)

target_include_directories(
  local_acr_core
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
  PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${PROJECT_SOURCE_DIR}/third_party/kissfft"
    "${PROJECT_SOURCE_DIR}/third_party/speexdsp/include"
    "${PROJECT_SOURCE_DIR}/third_party/sqlite"
)

target_compile_features(local_acr_core PUBLIC cxx_std_20)
target_link_libraries(local_acr_core PRIVATE lacr_kissfft lacr_speexdsp lacr_sqlite)

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
