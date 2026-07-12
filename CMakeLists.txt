cmake_minimum_required(VERSION 3.16)
project(xadv2_engine VERSION 0.9.0 LANGUAGES CXX)

# This project is built in two situations, and it must behave differently in each:
#
#   * standalone  — engine development: build the examples, experiments and tests.
#   * as a subproject — a GAME's build did `add_subdirectory(<engine>)` (or
#     FetchContent) to compile the engine from source alongside itself. It wants
#     the library and nothing else.
#
# PROJECT_IS_TOP_LEVEL is CMake >= 3.21; define it ourselves for 3.16..3.20.
if(NOT DEFINED PROJECT_IS_TOP_LEVEL)
    if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
        set(PROJECT_IS_TOP_LEVEL ON)
    else()
        set(PROJECT_IS_TOP_LEVEL OFF)
    endif()
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# Export compile_commands.json for clangd / tooling.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(PAC_BUILD_TESTS "Build the test suite" ${PROJECT_IS_TOP_LEVEL})
option(PAC_BUILD_EXAMPLES "Build the examples" ${PROJECT_IS_TOP_LEVEL})
option(PAC_BUILD_EXPERIMENTS "Build the experiments" ${PROJECT_IS_TOP_LEVEL})
option(PAC_INSTALL "Generate install + export rules for the engine" ${PROJECT_IS_TOP_LEVEL})
option(PAC_BUILD_SHARED "Build the engine as a shared library" OFF)
option(PAC_ENABLE_WARNINGS "Enable -Wall -Wextra -Wpedantic" ON)
option(PAC_ENABLE_SANITIZERS "Enable ASan + UBSan (development only)" OFF)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

# Warnings are the ENGINE's business, not its consumers'. This target is linked
# PRIVATE by engine-owned targets and is never exported: a game that links
# pac::engine should not inherit -Wpedantic over its own code.
add_library(pac_warnings INTERFACE)
if(PAC_ENABLE_WARNINGS)
    target_compile_options(pac_warnings INTERFACE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic>)
endif()

# Sanitizers, on the other hand, MUST propagate: mixing ASan and non-ASan
# translation units is unsafe, so anything linking a sanitized engine has to be
# sanitized too. Hence this one is PUBLIC on the engine and part of the export —
# empty and harmless when the option is off.
add_library(pac_sanitizers INTERFACE)
set_target_properties(pac_sanitizers PROPERTIES EXPORT_NAME sanitizers)
add_library(pac::sanitizers ALIAS pac_sanitizers)
if(PAC_ENABLE_SANITIZERS)
    target_compile_options(pac_sanitizers INTERFACE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-fsanitize=address,undefined -fno-omit-frame-pointer>)
    target_link_options(pac_sanitizers INTERFACE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-fsanitize=address,undefined>)
endif()

# Kept as the in-tree convenience alias for "warnings + sanitizers", which is what
# every engine-owned target (examples, experiments, tests) wants. Not exported.
add_library(pac_project_options INTERFACE)
target_compile_features(pac_project_options INTERFACE cxx_std_20)
target_link_libraries(pac_project_options INTERFACE pac_warnings pac_sanitizers)

include(FetchContent)

add_subdirectory(lib)

if(PAC_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(PAC_BUILD_EXPERIMENTS)
    add_subdirectory(experiments)
endif()

if(PAC_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
