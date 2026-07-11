if(NOT DEFINED LACR_SOURCE_DIR)
  message(FATAL_ERROR "LACR_SOURCE_DIR is required")
endif()

set(lock_path "${LACR_SOURCE_DIR}/third_party/dependencies.lock.json")
if(NOT EXISTS "${lock_path}")
  message(FATAL_ERROR "Dependency lock does not exist: ${lock_path}")
endif()

file(READ "${lock_path}" lock_json)
string(JSON schema_version GET "${lock_json}" schemaVersion)
string(JSON dependency_count LENGTH "${lock_json}" dependencies)

if(NOT schema_version EQUAL 1)
  message(FATAL_ERROR "Expected dependency lock schema version 1")
endif()

if(NOT dependency_count EQUAL 3)
  message(FATAL_ERROR "Expected exactly three native dependencies")
endif()

set(expected_names kissfft speexdsp sqlite)
set(expected_versions 131.2.0 1.2.1 3.53.3)
set(
  expected_sha256
  205a8f6a448ef12b091f8ac6a514b5091bb5f6b0b543431ed75f673116cf5cbf
  8c777343e4a6399569c72abc38a95b24db56882c83dbdb6c6424a5f4aeb54d3d
  646421e12aac110282ef8cc68f1a62d4bb15fc7b8f09da0b53e29ee690500431
)

foreach(index RANGE 0 2)
  list(GET expected_names "${index}" expected_name)
  list(GET expected_versions "${index}" expected_version)
  list(GET expected_sha256 "${index}" expected_digest)

  string(JSON actual_name GET "${lock_json}" dependencies "${index}" name)
  string(JSON actual_version GET "${lock_json}" dependencies "${index}" version)
  string(JSON actual_digest GET "${lock_json}" dependencies "${index}" sha256)
  string(JSON notice_path GET "${lock_json}" dependencies "${index}" noticePath)

  if(NOT actual_name STREQUAL expected_name)
    message(FATAL_ERROR "Dependency ${index} name mismatch")
  endif()
  if(NOT actual_version STREQUAL expected_version)
    message(FATAL_ERROR "Dependency ${actual_name} version mismatch")
  endif()
  if(NOT actual_digest STREQUAL expected_digest)
    message(FATAL_ERROR "Dependency ${actual_name} SHA-256 mismatch")
  endif()
  if(NOT EXISTS "${LACR_SOURCE_DIR}/${notice_path}")
    message(FATAL_ERROR "Dependency ${actual_name} notice does not exist: ${notice_path}")
  endif()
endforeach()
