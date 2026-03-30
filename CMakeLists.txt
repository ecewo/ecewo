cmake_minimum_required(VERSION 3.14)
project(ecewo VERSION 4.0.0 LANGUAGES C)

if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW) # DOWNLOAD_EXTRACT_TIMESTAMP
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message(STATUS "Setting build type to 'RelWithDebInfo' as none was specified.")
  set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "Choose the type of build." FORCE)
endif()

option(ECEWO_BUILD_SHARED "Build shared library instead of static" OFF)
option(ECEWO_BUILD_TESTS "Build tests" OFF)

option(ECEWO_ASAN "Enable AddressSanitizer" OFF)
option(ECEWO_MSAN "Enable MemorySanitizer" OFF)
option(ECEWO_TSAN "Enable ThreadSanitizer" OFF)
option(ECEWO_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)

if(ECEWO_MSAN AND NOT CMAKE_C_COMPILER_ID MATCHES "AppleClang|Clang")
  message(SEND_ERROR "MemorySanitizer requires clang. Try again with -DCMAKE_C_COMPILER=clang")
endif()

if(ECEWO_ASAN OR ECEWO_MSAN OR ECEWO_TSAN OR ECEWO_UBSAN)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fno-omit-frame-pointer")
  set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -fno-omit-frame-pointer")
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fno-omit-frame-pointer")
endif()

if(ECEWO_ASAN)
  if(CMAKE_C_COMPILER_ID MATCHES "AppleClang|GNU|Clang")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -fsanitize=address")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address")
  else()
    message(SEND_ERROR "AddressSanitizer support requires clang or gcc. Try again with -DCMAKE_C_COMPILER.")
  endif()
endif()

if(ECEWO_MSAN)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=memory")
  set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -fsanitize=memory")
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=memory")
endif()

if(ECEWO_TSAN)
  if(CMAKE_C_COMPILER_ID MATCHES "AppleClang|GNU|Clang")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=thread")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -fsanitize=thread")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=thread")
  else()
    message(SEND_ERROR "ThreadSanitizer support requires clang or gcc. Try again with -DCMAKE_C_COMPILER.")
  endif()
endif()

if(ECEWO_UBSAN)
  if(CMAKE_C_COMPILER_ID MATCHES "AppleClang|GNU|Clang")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=undefined -fno-sanitize-recover=undefined")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -fsanitize=undefined")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=undefined")
  else()
    message(SEND_ERROR "UndefinedBehaviorSanitizer support requires clang or gcc. Try again with -DCMAKE_C_COMPILER.")
  endif()
endif()

set(ECEWO_SANITIZER_ENV "")
if(ECEWO_ASAN)
  list(APPEND ECEWO_SANITIZER_ENV "ASAN_OPTIONS=detect_leaks=1:halt_on_error=1:abort_on_error=1")
endif()
if(ECEWO_UBSAN)
  list(APPEND ECEWO_SANITIZER_ENV "UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1")
endif()
if(ECEWO_MSAN)
  list(APPEND ECEWO_SANITIZER_ENV "MSAN_OPTIONS=halt_on_error=1:print_stacktrace=1")
endif()
if(ECEWO_TSAN)
  list(APPEND ECEWO_SANITIZER_ENV "TSAN_OPTIONS=halt_on_error=1:print_stacktrace=1")
endif()

include(FetchContent)

FetchContent_Declare(
  libuv
  GIT_REPOSITORY https://github.com/libuv/libuv.git
  GIT_TAG v1.52.0
  GIT_SHALLOW TRUE
)

FetchContent_Declare(
  llhttp
  URL "https://github.com/nodejs/llhttp/archive/refs/tags/release/v9.3.0.tar.gz"
)

if(ECEWO_BUILD_SHARED)
  set(LIBUV_BUILD_SHARED ON CACHE BOOL "" FORCE)
  set(LLHTTP_BUILD_SHARED_LIBS ON CACHE INTERNAL "" FORCE)
  set(LLHTTP_BUILD_STATIC_LIBS OFF CACHE INTERNAL "" FORCE)
  set(BUILD_SHARED_LIBS ON CACHE INTERNAL "" FORCE)
  set(BUILD_STATIC_LIBS OFF CACHE INTERNAL "" FORCE)
else()
  set(LIBUV_BUILD_SHARED OFF CACHE BOOL "" FORCE)
  set(LLHTTP_BUILD_SHARED_LIBS OFF CACHE INTERNAL "" FORCE)
  set(LLHTTP_BUILD_STATIC_LIBS ON CACHE INTERNAL "" FORCE)
  set(BUILD_SHARED_LIBS OFF CACHE INTERNAL "" FORCE)
  set(BUILD_STATIC_LIBS ON CACHE INTERNAL "" FORCE)
endif()

FetchContent_MakeAvailable(libuv llhttp)

if(NOT TARGET ecewo::ecewo)
  set(ECEWO_SOURCES
    src/server.c
    src/http.c
    src/request.c
    src/response.c
    src/router.c
    src/middleware.c
    src/route-table.c
    src/route-register.c
    src/spawn.c
    src/body.c
    src/utils/date-cache.c
    src/utils/url-decode.c
    src/arena/arena.c
    src/arena/arena-pool.c
    vendor/rax.c
  )

  if(ECEWO_BUILD_SHARED)
    add_library(ecewo SHARED ${ECEWO_SOURCES})
    message(STATUS "Building ecewo as SHARED library")
  else()
    add_library(ecewo STATIC ${ECEWO_SOURCES})
    message(STATUS "Building ecewo as STATIC library")
  endif()

  add_library(ecewo::ecewo ALIAS ecewo)
  include(${CMAKE_CURRENT_LIST_DIR}/cmake/plugins.cmake)

  if(ECEWO_BUILD_TESTS)
    target_compile_definitions(ecewo PRIVATE ECEWO_TEST_MODE=1)
  endif()

  if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(ecewo PRIVATE
      -fno-strict-aliasing
      -Wunused-function
      -Wunused-variable
      -Werror=unused-function
      -Werror=unused-variable
    )
  endif()

  if(CMAKE_C_COMPILER_ID MATCHES "Clang|AppleClang")
    target_compile_options(ecewo PRIVATE
      -Wunreachable-code
      -Werror=unreachable-code
    )
  endif()

  target_include_directories(ecewo 
    PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
      $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
      $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/_deps/libuv-src/include>
      $<INSTALL_INTERFACE:include>
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/src
      ${CMAKE_CURRENT_SOURCE_DIR}/src/utils
      ${CMAKE_CURRENT_SOURCE_DIR}/src/arena
      ${CMAKE_CURRENT_SOURCE_DIR}/vendor
      ${CMAKE_BINARY_DIR}/_deps/llhttp-src/include
  )

  if(ECEWO_BUILD_SHARED)
    target_link_libraries(ecewo PUBLIC uv llhttp_shared)
  else()
    target_link_libraries(ecewo PUBLIC uv_a llhttp_static)
  endif()

  if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(ecewo PRIVATE ECEWO_DEBUG=1)
  endif()

  set_target_properties(ecewo PROPERTIES
    OUTPUT_NAME ecewo
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    EXPORT_NAME ecewo
    C_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    POSITION_INDEPENDENT_CODE ON
  )
endif()

if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
  message(STATUS "====== Build Configuration ======")
  message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
  message(STATUS "Library type: ${ECEWO_BUILD_SHARED}")
  message(STATUS "Target system: ${CMAKE_SYSTEM_NAME}")
  message(STATUS "Compiler: ${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}")
  message(STATUS "=================================")
endif()

option(ECEWO_BUILD_FUZZ "Build libFuzzer fuzz targets (requires Clang)" OFF)

if(ECEWO_BUILD_FUZZ)
  if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang|AppleClang")
    message(FATAL_ERROR
      "ECEWO_BUILD_FUZZ requires Clang: libFuzzer is Clang-only.\n"
      "Re-run with -DCMAKE_C_COMPILER=clang")
  endif()

  foreach(fuzz_target fuzz-router fuzz-route-register)
    if(fuzz_target STREQUAL "fuzz-router")
      set(fuzz_src fuzz/fuzz-router.c)
    else()
      set(fuzz_src fuzz/fuzz-route-register.c)
    endif()

    add_executable(${fuzz_target} ${fuzz_src})

    target_compile_options(${fuzz_target} PRIVATE
      -fsanitize=fuzzer,address,undefined
      -fno-omit-frame-pointer
      -g
    )

    target_link_options(${fuzz_target} PRIVATE
      -fsanitize=fuzzer,address,undefined
    )

    target_link_libraries(${fuzz_target} PRIVATE ecewo::ecewo)

    target_include_directories(${fuzz_target} PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/src
      ${CMAKE_CURRENT_SOURCE_DIR}/src/arena
      ${CMAKE_BINARY_DIR}/_deps/llhttp-src/include
    )

    message(STATUS "Fuzz target: ${fuzz_target}")
  endforeach()
endif()

if(ECEWO_BUILD_TESTS)
  include(CTest)
  enable_testing()
  
  ecewo_plugin(mock)
  
  function(ecewo_test test_name)
    set(target_name ecewo-test-${test_name})
    
    add_executable(${target_name}
      tests/test-${test_name}.c
    )
    
    target_link_libraries(${target_name} PRIVATE
      ecewo::ecewo
      ecewo::mock
    )
    
    target_include_directories(${target_name} PRIVATE
      ${CMAKE_SOURCE_DIR}/tests
    )
    
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
      target_compile_options(${target_name} PRIVATE
        -Wall -Wextra -Werror
        -Wstrict-prototypes
        -Wincompatible-pointer-types
        -Wno-unused-parameter
      )
    endif()
    
    add_test(NAME ${test_name} COMMAND ${target_name})
    if(ECEWO_SANITIZER_ENV)
      set_tests_properties(${test_name} PROPERTIES ENVIRONMENT "${ECEWO_SANITIZER_ENV}")
    endif()
  endfunction()
  
  ecewo_test(arena)
  ecewo_test(async-middleware)
  ecewo_test(body-buffering)
  ecewo_test(body-streaming)
  ecewo_test(body-streaming-split)
  ecewo_test(blocking)
  ecewo_test(concurrent-request)
  ecewo_test(router)
  ecewo_test(router-stress)
  ecewo_test(router-edge)
  ecewo_test(context)
  ecewo_test(fire-and-forget)
  ecewo_test(headers)
  ecewo_test(methods)
  ecewo_test(middleware)
  ecewo_test(use-middleware)
  ecewo_test(params)
  ecewo_test(query)
  ecewo_test(redirect)
  ecewo_test(response)
  ecewo_test(root)
  ecewo_test(sb)
  ecewo_test(task-parallel)
  ecewo_test(task)
  ecewo_test(url-decode)
  ecewo_test(405)
endif()
