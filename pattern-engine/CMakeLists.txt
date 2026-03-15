cmake_minimum_required(VERSION 3.14)
project(PatternEngine C)

include(FetchContent)

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------

# PCRE2
find_package(PkgConfig REQUIRED)
pkg_check_modules(PCRE2 REQUIRED IMPORTED_TARGET libpcre2-8)

# Threads
find_package(Threads REQUIRED)

# cJSON: use existing target if available (e.g. when added as subdirectory
# of the Nabto SDK which provides a proper cjson target with include dirs).
if(NOT TARGET cjson)
    FetchContent_Declare(cjson
        GIT_REPOSITORY https://github.com/DaveGamble/cJSON.git
        GIT_TAG v1.7.18)
    FetchContent_GetProperties(cjson)
    if(NOT cjson_POPULATED)
        FetchContent_Populate(cjson)
        # Build a minimal static library. The upstream CMakeLists.txt does
        # not export include directories the way we need (cjson/cJSON.h).
        # Create a wrapper include directory so #include <cjson/cJSON.h> works.
        set(_cjson_inc_dir "${CMAKE_CURRENT_BINARY_DIR}/cjson_include")
        file(MAKE_DIRECTORY "${_cjson_inc_dir}/cjson")
        file(COPY "${cjson_SOURCE_DIR}/cJSON.h" DESTINATION "${_cjson_inc_dir}/cjson")
        add_library(cjson STATIC ${cjson_SOURCE_DIR}/cJSON.c)
        target_include_directories(cjson PUBLIC ${_cjson_inc_dir} ${cjson_SOURCE_DIR})
    endif()
endif()

# TinyCBOR: when fetched by Nabto SDK it creates TinyCBOR::tinycbor alias
# with proper include dirs. For standalone builds we fetch from the upstream
# Intel repo and create a minimal target ourselves (upstream uses Makefiles).
if(NOT TARGET tinycbor AND NOT TARGET TinyCBOR::tinycbor)
    FetchContent_Declare(tinycbor
        GIT_REPOSITORY https://github.com/intel/tinycbor.git
        GIT_TAG v0.6.0)
    FetchContent_GetProperties(tinycbor)
    if(NOT tinycbor_POPULATED)
        FetchContent_Populate(tinycbor)
        add_library(tinycbor STATIC
            ${tinycbor_SOURCE_DIR}/src/cborencoder.c
            ${tinycbor_SOURCE_DIR}/src/cborparser.c
            ${tinycbor_SOURCE_DIR}/src/cborparser_dup_string.c
            ${tinycbor_SOURCE_DIR}/src/cbortojson.c
            ${tinycbor_SOURCE_DIR}/src/cborvalidation.c)
        # Create wrapper so #include <tinycbor/cbor.h> works.
        set(_tcbor_inc_dir "${CMAKE_CURRENT_BINARY_DIR}/tinycbor_include")
        file(MAKE_DIRECTORY "${_tcbor_inc_dir}/tinycbor")
        file(GLOB _tcbor_headers "${tinycbor_SOURCE_DIR}/src/*.h")
        file(COPY ${_tcbor_headers} DESTINATION "${_tcbor_inc_dir}/tinycbor")
        target_include_directories(tinycbor PUBLIC
            ${_tcbor_inc_dir} ${tinycbor_SOURCE_DIR}/src)
        add_library(TinyCBOR::tinycbor ALIAS tinycbor)
    endif()
endif()

# Resolve the TinyCBOR target name (alias vs plain).
if(TARGET TinyCBOR::tinycbor)
    set(TINYCBOR_TARGET TinyCBOR::tinycbor)
else()
    set(TINYCBOR_TARGET tinycbor)
endif()

# ---------------------------------------------------------------------------
# Library
# ---------------------------------------------------------------------------

add_library(pe_prompt STATIC
    src/pe_prompt.c
    src/pe_pattern_config.c
    src/pe_terminal_state.c
    src/pe_prompt_rules.c
    src/pe_prompt_lifecycle.c
    src/pe_detector.c
    src/pe_protocol.c
)

target_include_directories(pe_prompt
    PUBLIC  include/
    PRIVATE src/
)

target_link_libraries(pe_prompt
    PUBLIC
        cjson
        PkgConfig::PCRE2
        ${TINYCBOR_TARGET}
        Threads::Threads
)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

option(PE_BUILD_TESTS "Build pattern-engine tests" ON)

if(PE_BUILD_TESTS)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(CHECK IMPORTED_TARGET check)

    if(CHECK_FOUND)
        enable_testing()

        set(PE_TEST_SOURCES
            test_pattern_config
            test_terminal_state
            test_prompt_rules
            test_prompt_lifecycle
            test_protocol
            test_detector
        )

        foreach(test_name IN LISTS PE_TEST_SOURCES)
            add_executable(${test_name} tests/${test_name}.c)
            target_link_libraries(${test_name} PRIVATE pe_prompt PkgConfig::CHECK)
            add_test(NAME ${test_name} COMMAND ${test_name})
        endforeach()

        # Replay test needs the fixtures directory path.
        add_executable(test_detector_replay tests/test_detector_replay.c)
        target_link_libraries(test_detector_replay PRIVATE pe_prompt PkgConfig::CHECK)
        target_compile_definitions(test_detector_replay PRIVATE
            TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures"
        )
        add_test(NAME test_detector_replay COMMAND test_detector_replay)
    endif()
endif()
