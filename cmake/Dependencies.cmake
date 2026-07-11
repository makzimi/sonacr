include_guard(GLOBAL)

include(FetchContent)

function(lacr_verify_archive archive_path expected_sha256)
  if(NOT EXISTS "${archive_path}")
    message(FATAL_ERROR "Dependency archive not found: ${archive_path}")
  endif()

  file(SHA256 "${archive_path}" actual_sha256)
  string(TOLOWER "${expected_sha256}" normalized_expected_sha256)

  if(NOT actual_sha256 STREQUAL normalized_expected_sha256)
    message(
      FATAL_ERROR
      "Dependency SHA-256 mismatch for ${archive_path}: "
      "expected ${normalized_expected_sha256}, got ${actual_sha256}"
    )
  endif()
endfunction()

function(lacr_declare_verified_dependency dependency_name archive_url archive_sha256)
  FetchContent_Declare(
    "${dependency_name}"
    URL "${archive_url}"
    URL_HASH "SHA256=${archive_sha256}"
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  )
endfunction()
