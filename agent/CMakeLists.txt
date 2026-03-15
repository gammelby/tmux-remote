cmake_minimum_required(VERSION 3.14)
project(TmuxRemoteAgent C)

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

set(PE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../pattern-engine pattern-engine)

set(src
  ${ne_iam_src}
  src/main.c
  src/tmuxremote_device.c
  src/tmuxremote_iam.c
  src/tmuxremote_init.c
  src/tmuxremote_coap_handler.c
  src/tmuxremote_coap_resize.c
  src/tmuxremote_coap_sessions.c
  src/tmuxremote_coap_attach.c
  src/tmuxremote_coap_create.c
  src/tmuxremote_coap_status.c
  src/tmuxremote_control_stream.c
  src/tmuxremote_stream.c
  src/tmuxremote_session.c
  src/tmuxremote_tmux.c
  src/tmuxremote_banner.c
  src/tmuxremote_keychain.c
)

add_executable(tmux-remote-agent ${src})

target_include_directories(tmux-remote-agent PRIVATE
  ${NABTO_SDK_SRC}/src
  ${NABTO_SDK_SRC}/include
  ${NABTO_SDK_SRC}
)

target_link_libraries(tmux-remote-agent
  nabto_device
  3rdparty_gopt
  NabtoCommon::nn
  apps_common
  nm_fs_posix
  pe_prompt
)

if(APPLE)
  target_link_libraries(tmux-remote-agent "-framework Security" "-framework CoreFoundation")
else()
  target_link_libraries(tmux-remote-agent util)
endif()

install(TARGETS tmux-remote-agent RUNTIME DESTINATION bin)

