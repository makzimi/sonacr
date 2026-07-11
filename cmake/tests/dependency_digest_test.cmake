if(NOT DEFINED LACR_SOURCE_DIR OR NOT DEFINED LACR_TEST_DIR)
  message(FATAL_ERROR "LACR_SOURCE_DIR and LACR_TEST_DIR are required")
endif()

include("${LACR_SOURCE_DIR}/cmake/Dependencies.cmake")

file(MAKE_DIRECTORY "${LACR_TEST_DIR}")
set(archive "${LACR_TEST_DIR}/archive.bin")
file(WRITE "${archive}" "local-acr")
file(SHA256 "${archive}" expected_sha256)

lacr_verify_archive("${archive}" "${expected_sha256}")

execute_process(
  COMMAND
    "${CMAKE_COMMAND}"
    "-DLACR_DEPENDENCIES_MODULE=${LACR_SOURCE_DIR}/cmake/Dependencies.cmake"
    "-DLACR_ARCHIVE=${archive}"
    -P
    "${LACR_SOURCE_DIR}/cmake/tests/dependency_digest_mismatch.cmake"
  RESULT_VARIABLE mismatch_result
  OUTPUT_VARIABLE mismatch_stdout
  ERROR_VARIABLE mismatch_stderr
)

if(mismatch_result EQUAL 0)
  message(FATAL_ERROR "A mismatching dependency archive was accepted")
endif()

string(CONCAT mismatch_output "${mismatch_stdout}" "${mismatch_stderr}")
if(NOT mismatch_output MATCHES "Dependency SHA-256 mismatch")
  message(FATAL_ERROR "Unexpected mismatch diagnostic: ${mismatch_output}")
endif()
