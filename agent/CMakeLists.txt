cmake_minimum_required(VERSION 3.14)
project(NabtoShellAgent C)

include(FetchContent)

FetchContent_Declare(
  nabto_embedded_sdk
  GIT_REPOSITORY https://github.com/nabto/nabto-embedded-sdk.git
  GIT_TAG        master
)

set(DEVICE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(DEVICE_BUILD_APPS OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(nabto_embedded_sdk)

FetchContent_GetProperties(nabto_embedded_sdk SOURCE_DIR NABTO_SDK_SRC)
include(${NABTO_SDK_SRC}/nabto_primary_files.cmake)

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(PCRE2 REQUIRED IMPORTED_TARGET libpcre2-8)

# Pattern detection library (shared between agent and tests)
set(pattern_src
  src/nabtoshell_ansi_stripper.c
  src/nabtoshell_rolling_buffer.c
  src/nabtoshell_pattern_config.c
  src/nabtoshell_pattern_matcher.c
  src/nabtoshell_pattern_engine.c
  src/nabtoshell_pattern_cbor.c
)

set(src
  ${ne_iam_src}
  src/main.c
  src/nabtoshell_device.c
  src/nabtoshell_iam.c
  src/nabtoshell_init.c
  src/nabtoshell_coap_handler.c
  src/nabtoshell_coap_resize.c
  src/nabtoshell_coap_sessions.c
  src/nabtoshell_coap_attach.c
  src/nabtoshell_coap_create.c
  src/nabtoshell_coap_status.c
  src/nabtoshell_control_stream.c
  src/nabtoshell_stream.c
  src/nabtoshell_session.c
  src/nabtoshell_tmux.c
  src/nabtoshell_banner.c
  ${pattern_src}
)

add_executable(nabtoshell-agent ${src})

target_include_directories(nabtoshell-agent PRIVATE
  ${NABTO_SDK_SRC}/src
  ${NABTO_SDK_SRC}/include
  ${NABTO_SDK_SRC}
)

target_link_libraries(nabtoshell-agent
  nabto_device
  3rdparty_gopt
  cjson
  NabtoCommon::nn
  apps_common
  nm_fs_posix
  TinyCBOR::tinycbor
  Threads::Threads
  PkgConfig::PCRE2
)

if(NOT APPLE)
  target_link_libraries(nabtoshell-agent util)
endif()

install(TARGETS nabtoshell-agent RUNTIME DESTINATION bin)

# Tests (only if Check is found)
pkg_check_modules(CHECK IMPORTED_TARGET check)
if(CHECK_FOUND)
  enable_testing()

  # Static library for pattern detection (no Nabto SDK dependency)
  add_library(nabtoshell_pattern STATIC ${pattern_src})
  target_include_directories(nabtoshell_pattern PUBLIC src)
  target_link_libraries(nabtoshell_pattern PUBLIC
    cjson PkgConfig::PCRE2 TinyCBOR::tinycbor Threads::Threads)

  # Test: ANSI Stripper
  add_executable(test_ansi_stripper tests/test_ansi_stripper.c)
  target_link_libraries(test_ansi_stripper nabtoshell_pattern PkgConfig::CHECK)
  add_test(NAME ansi_stripper COMMAND test_ansi_stripper)

  # Test: Pattern Config
  add_executable(test_pattern_config tests/test_pattern_config.c)
  target_link_libraries(test_pattern_config nabtoshell_pattern PkgConfig::CHECK)
  add_test(NAME pattern_config COMMAND test_pattern_config)

  # Test: Pattern Matcher
  add_executable(test_pattern_matcher tests/test_pattern_matcher.c)
  target_link_libraries(test_pattern_matcher nabtoshell_pattern PkgConfig::CHECK)
  add_test(NAME pattern_matcher COMMAND test_pattern_matcher)

  # Test: Pattern Engine
  add_executable(test_pattern_engine tests/test_pattern_engine.c)
  target_link_libraries(test_pattern_engine nabtoshell_pattern PkgConfig::CHECK)
  add_test(NAME pattern_engine COMMAND test_pattern_engine)

  # Test: Pattern Broadcast (CBOR roundtrip, integration, thread safety)
  add_executable(test_pattern_broadcast tests/test_pattern_broadcast.c)
  target_link_libraries(test_pattern_broadcast nabtoshell_pattern PkgConfig::CHECK)
  add_test(NAME pattern_broadcast COMMAND test_pattern_broadcast)

  # Test: Pattern Routing (per-stream isolation, ref-scoped delivery)
  add_executable(test_pattern_routing tests/test_pattern_routing.c)
  target_link_libraries(test_pattern_routing nabtoshell_pattern PkgConfig::CHECK)
  target_include_directories(test_pattern_routing PRIVATE
    ${NABTO_SDK_SRC}/include
  )
  target_compile_definitions(test_pattern_routing PRIVATE NABTOSHELL_TESTING)
  add_test(NAME pattern_routing COMMAND test_pattern_routing)
endif()
