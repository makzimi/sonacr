cmake_minimum_required(VERSION 3.28)

project(LocalAcr LANGUAGES C CXX)

option(LACR_BUILD_TESTS "Build Local ACR native tests" ON)
option(LACR_ENABLE_ASAN_UBSAN "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(LACR_ENABLE_TSAN "Enable ThreadSanitizer" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(CompilerWarnings)
include(Sanitizers)
include(StrictFp)

if(LACR_ENABLE_ASAN_UBSAN AND LACR_ENABLE_TSAN)
  message(FATAL_ERROR "ASan/UBSan and TSan must use separate build trees")
endif()

if(LACR_BUILD_TESTS)
  include(CTest)
  enable_testing()
endif()

add_subdirectory(native/core)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "iOS" AND NOT CMAKE_SYSTEM_NAME STREQUAL "Android")
  add_subdirectory(native/cli)
endif()

if(LACR_BUILD_TESTS)
  add_subdirectory(native/tests)
endif()
