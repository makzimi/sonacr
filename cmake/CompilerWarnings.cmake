function(lacr_apply_warnings target)
  if(CMAKE_CXX_COMPILER_ID MATCHES "AppleClang|Clang|GNU")
    target_compile_options(
      "${target}"
      PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Werror
        -Wshadow
        -Wsign-conversion
    )
  else()
    message(FATAL_ERROR "Unsupported compiler for strict warning policy: ${CMAKE_CXX_COMPILER_ID}")
  endif()
endfunction()
