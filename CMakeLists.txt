#
#                    cgeonames
#           See GeoNames in pure C
#  ╔═══════════════════════════════════════════╗
#  ║                                           ║
#  ║          .  *  .   *   .  *  .            ║
#  ║       *        *        .      *          ║
#  ║           ___ /|\ ___                     ║
#  ║         .'   / | \   `.                   ║
#  ║        /   .'  |  `.   \                  ║
#  ║       |   /    |    \   |                 ║
#  ║        \  \    |    /  /                  ║
#  ║         `. `.__|__.' .´                   ║
#  ║           `--_____--´                     ║
#  ║       .    *   .    *      .    *         ║
#  ║                                           ║
#  ║    35.6895N  139.6917E  Tokyo, JST        ║
#  ║                                           ║
#  ╚═══════════════════════════════════════════╝
#
#  https://github.com/LuxKingSoft/cgeonames
#

cmake_minimum_required(VERSION 3.10)
project(cgeonames VERSION 1.0.0 LANGUAGES C)

# ============================================================================
# Options
# ============================================================================
option(CGEONAMES_BUILD_EXAMPLES "Build example programs" ON)
option(CGEONAMES_BUILD_TESTS    "Build test programs"    ON)

# ============================================================================
# Dependencies
# ============================================================================

# --- libcurl (required for network downloads) ---
find_package(CURL REQUIRED)

# --- Math library on Unix (-lm) ---
find_library(MATH_LIB m)
if(NOT MATH_LIB)
    set(MATH_LIB "")
endif()

# ============================================================================
# Source / header lists
# ============================================================================
set(CGEONAMES_SOURCES
    source/cgeonames.c
)

set(CGEONAMES_HEADERS
    include/cgeonames.h
)

# ============================================================================
# Status output
# ============================================================================

# ANSI colour codes for the ASCII art (blue sky theme)
string(ASCII 27 ESCAPE)
set(BLUE    "${ESCAPE}[1;34m")
set(CYAN    "${ESCAPE}[1;36m")
set(WHITE   "${ESCAPE}[1;37m")
set(RESET   "${ESCAPE}[0m")

message(STATUS "")
message(STATUS "${BLUE}           CGEONAMES v${PROJECT_VERSION}${RESET}")
message(STATUS "${CYAN}                See GeoNames in C${RESET}")
message(STATUS "${BLUE}    ╔════════════════════════════════════════╗${RESET}")
message(STATUS "${BLUE}    ║                                        ║${RESET}")
message(STATUS "${BLUE}    ║      ${WHITE}    .  *  .   *   .  *  .         ${BLUE}║${RESET}")
message(STATUS "${BLUE}    ║      ${WHITE} *        *        .      *       ${BLUE}║${RESET}")
message(STATUS "${CYAN}    ║           ___ /|\\ ___                  ║${RESET}")
message(STATUS "${CYAN}    ║         .'   / | \\   `.                ║${RESET}")
message(STATUS "${CYAN}    ║        /   .'  |  `.   \\               ║${RESET}")
message(STATUS "${CYAN}    ║       |   /    |    \\   |              ║${RESET}")
message(STATUS "${CYAN}    ║        \\  \\    |    /  /               ║${RESET}")
message(STATUS "${CYAN}    ║         `. `.__|__.' .´                ║${RESET}")
message(STATUS "${CYAN}    ║           `--_____--´                  ║${RESET}")
message(STATUS "${BLUE}    ║      ${WHITE} .    *   .    *      .    *      ${BLUE}║${RESET}")
message(STATUS "${BLUE}    ║                                        ║${RESET}")
message(STATUS "${BLUE}    ║   ${WHITE} 35.6895N  139.6917E  Tokyo, JST     ${BLUE}║${RESET}")
message(STATUS "${BLUE}    ║                                        ║${RESET}")
message(STATUS "${BLUE}    ╚════════════════════════════════════════╝${RESET}")
message(STATUS "")
message(STATUS "${CYAN}    https://github.com/LuxKingSoft/cgeonames${RESET}")
message(STATUS "")
message(STATUS "  cURL include dirs : ${CURL_INCLUDE_DIRS}")
message(STATUS "  cURL libraries   : ${CURL_LIBRARIES}")
message(STATUS "  Math library      : ${MATH_LIB}")
message(STATUS "")

# ============================================================================
# Static library
# ============================================================================
add_library(cgeonames_static STATIC ${CGEONAMES_SOURCES})

target_include_directories(cgeonames_static
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CURL_INCLUDE_DIRS}
)

target_link_libraries(cgeonames_static
    PRIVATE ${CURL_LIBRARIES}
    PRIVATE ${MATH_LIB}
)

set_target_properties(cgeonames_static PROPERTIES
    OUTPUT_NAME "cgeonames"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/libs"
)

# ============================================================================
# Shared library
# ============================================================================
add_library(cgeonames_shared SHARED ${CGEONAMES_SOURCES})

target_include_directories(cgeonames_shared
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CURL_INCLUDE_DIRS}
)

target_link_libraries(cgeonames_shared
    PRIVATE ${CURL_LIBRARIES}
    PRIVATE ${MATH_LIB}
)

set_target_properties(cgeonames_shared PROPERTIES
    OUTPUT_NAME "cgeonames"
    SOVERSION "1"
    VERSION "${PROJECT_VERSION}"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/libs"
)

# ============================================================================
# Named aliases — used by parent CMakeLists via target_link_libraries
# ============================================================================
add_library(cgeonames::static  ALIAS cgeonames_static)
add_library(cgeonames::shared  ALIAS cgeonames_shared)

# ============================================================================
# Convenience "build everything" target
# ============================================================================
add_custom_target(build_cgeonames
    DEPENDS cgeonames_static cgeonames_shared
    COMMENT "Building cgeonames static and shared libraries..."
)

# ============================================================================
# Examples (built only when CGEONAMES_BUILD_EXAMPLES=ON)
# ============================================================================
if(CGEONAMES_BUILD_EXAMPLES)
    message(STATUS "Building cgeonames examples...")

    # Example 1: Basic city search with fuzzy matching
    add_executable(cgeonames_example_basic_search examples/basic_search.c)
    target_link_libraries(cgeonames_example_basic_search
        PRIVATE cgeonames_static
    )

    # Example 2: Reverse geocoding — lat/lon to nearest city
    add_executable(cgeonames_example_reverse_geocoding examples/reverse_geocoding.c)
    target_link_libraries(cgeonames_example_reverse_geocoding
        PRIVATE cgeonames_static
    )

    # Example 3: Advanced features — field filtering, progress callbacks, errors
    add_executable(cgeonames_example_advanced_features examples/advanced_features.c)
    target_link_libraries(cgeonames_example_advanced_features
        PRIVATE cgeonames_static
    )

    message(STATUS "  Examples: basic_search, reverse_geocoding, advanced_features")
endif()

# ============================================================================
# Tests ( built only when CGEONAMES_BUILD_TESTS=ON)
# ============================================================================
if(CGEONAMES_BUILD_TESTS)
    enable_testing()
    message(STATUS "Building cgeonames tests...")

    # Unity Test Framework files
    set(UNITY_SOURCES
        tests/unity.c
    )

    set(UNITY_HEADERS
        tests/unity.h
        tests/unity_internals.h
    )

    # Test executable: Engine lifecycle and core functions
    add_executable(cgeonames_test_engine
        tests/test_engine.c
        ${UNITY_SOURCES}
    )

    target_include_directories(cgeonames_test_engine
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )

    target_link_libraries(cgeonames_test_engine
        PRIVATE cgeonames_static
        PRIVATE ${MATH_LIB}
    )

    # Register test with CTest
    add_test(
        NAME cgeonames_test_engine
        COMMAND cgeonames_test_engine
    )

    # Set test properties
    set_tests_properties(cgeonames_test_engine PROPERTIES
        TIMEOUT 30
        LABELS "unit"
    )

    message(STATUS "  Tests: test_engine (Unity Test Framework)")
endif()
