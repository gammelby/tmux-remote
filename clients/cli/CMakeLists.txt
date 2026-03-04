cmake_minimum_required(VERSION 3.14)
project(TmuxRemoteCLI C)

if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()
if(POLICY CMP0169)
  cmake_policy(SET CMP0169 OLD)
endif()

find_package(Threads REQUIRED)

include(FetchContent)

FetchContent_Declare(
  tinycbor
  GIT_REPOSITORY https://github.com/intel/tinycbor.git
  GIT_TAG        v0.6.0
)
FetchContent_Populate(tinycbor)

FetchContent_Declare(
  nabto_client_sdk
  URL https://github.com/nabto/nabto-client-sdk-releases/archive/refs/tags/v5.12.0.tar.gz
)
FetchContent_MakeAvailable(nabto_client_sdk)

set(TINYCBOR_SRC
  ${tinycbor_SOURCE_DIR}/src/cborencoder.c
  ${tinycbor_SOURCE_DIR}/src/cborparser.c
  ${tinycbor_SOURCE_DIR}/src/cborparser_dup_string.c
  ${tinycbor_SOURCE_DIR}/src/cborvalidation.c
)
add_library(tinycbor_lib STATIC ${TINYCBOR_SRC})
target_include_directories(tinycbor_lib PUBLIC ${tinycbor_SOURCE_DIR}/src)

set(src
  src/main.c
  src/tmuxremote_pair.c
  src/tmuxremote_attach.c
  src/tmuxremote_sessions.c
  src/tmuxremote_devices.c
  src/tmuxremote_rename.c
  src/tmuxremote_client_config.c
  src/tmuxremote_client_coap.c
  src/tmuxremote_client_log.c
  src/tmuxremote_client_util.c
  src/3rdparty/cjson/cJSON.c
)

add_executable(tmux-remote ${src})

target_include_directories(tmux-remote PRIVATE
  ${nabto_client_sdk_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(APPLE)
  set(NABTO_CLIENT_SDK_LIB_DIR ${nabto_client_sdk_SOURCE_DIR}/lib/macos-universal)
elseif(UNIX)
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
    set(NABTO_CLIENT_SDK_LIB_DIR ${nabto_client_sdk_SOURCE_DIR}/lib/linux-aarch64)
  else()
    set(NABTO_CLIENT_SDK_LIB_DIR ${nabto_client_sdk_SOURCE_DIR}/lib/linux-x86_64)
  endif()
endif()

target_link_directories(tmux-remote PRIVATE ${NABTO_CLIENT_SDK_LIB_DIR})

target_link_libraries(tmux-remote
  nabto_client
  tinycbor_lib
  Threads::Threads
)

if(APPLE)
  set(_rpath "@loader_path")
else()
  set(_rpath "$ORIGIN")
endif()

set_target_properties(tmux-remote PROPERTIES
  BUILD_RPATH "${NABTO_CLIENT_SDK_LIB_DIR}"
  INSTALL_RPATH "${_rpath}"
)

install(TARGETS tmux-remote RUNTIME DESTINATION bin)
