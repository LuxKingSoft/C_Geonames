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
option(GEONAMES_BUILD_EXAMPLES "Build example programs" OFF)
option(GEONAMES_BUILD_TESTS    "Build test programs"    OFF)

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
set(GEONAMES_SOURCES
    source/geonames.c
)

set(GEONAMES_HEADERS
    include/geonames.h
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
add_library(geonames_static STATIC ${GEONAMES_SOURCES})

target_include_directories(geonames_static
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CURL_INCLUDE_DIRS}
)

target_link_libraries(geonames_static
    PRIVATE ${CURL_LIBRARIES}
    PRIVATE ${MATH_LIB}
)

set_target_properties(geonames_static PROPERTIES
    OUTPUT_NAME "geonames"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/libs"
)

# ============================================================================
# Shared library
# ============================================================================
add_library(geonames_shared SHARED ${GEONAMES_SOURCES})

target_include_directories(geonames_shared
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CURL_INCLUDE_DIRS}
)

target_link_libraries(geonames_shared
    PRIVATE ${CURL_LIBRARIES}
    PRIVATE ${MATH_LIB}
)

set_target_properties(geonames_shared PROPERTIES
    OUTPUT_NAME "geonames"
    SOVERSION "1"
    VERSION "${PROJECT_VERSION}"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/libs"
)

# ============================================================================
# Named aliases — used by parent CMakeLists via target_link_libraries
# ============================================================================
add_library(geonames::static  ALIAS geonames_static)
add_library(geonames::shared  ALIAS geonames_shared)

# ============================================================================
# Convenience "build everything" target
# ============================================================================
add_custom_target(build_geonames
    DEPENDS geonames_static geonames_shared
    COMMENT "Building geonames static and shared libraries..."
)

# ============================================================================
# Examples (built only when GEONAMES_BUILD_EXAMPLES=ON)
# ============================================================================
if(GEONAMES_BUILD_EXAMPLES)
    message(STATUS "Building geonames examples...")
    file(GLOB EXAMPLE_SOURCES examples/*.c)
    foreach(src ${EXAMPLE_SOURCES})
        get_filename_component(exe_name ${src} NAME_WE)
        add_executable(geonames_example_${exe_name} ${src})
        target_link_libraries(geonames_example_${exe_name}
            PRIVATE geonames_static
            PRIVATE ${CURL_LIBRARIES}
        )
    endforeach()
endif()
