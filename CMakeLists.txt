cmake_minimum_required(VERSION 3.16)
project(xadv2_engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# Export compile_commands.json for clangd / tooling.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(PAC_BUILD_TESTS "Build the test suite" ON)
option(PAC_ENABLE_WARNINGS "Enable -Wall -Wextra -Wpedantic" ON)
option(PAC_ENABLE_SANITIZERS "Enable ASan + UBSan (development only)" OFF)

# Shared compile/link options carried by every engine-owned target. Keeping them
# on one INTERFACE target ensures sanitizers are applied consistently across the
# library, the games, and the tests (mixing ASan and non-ASan TUs is unsafe).
add_library(pac_project_options INTERFACE)
target_compile_features(pac_project_options INTERFACE cxx_std_20)
if(PAC_ENABLE_WARNINGS)
    target_compile_options(pac_project_options INTERFACE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic>)
endif()
if(PAC_ENABLE_SANITIZERS)
    target_compile_options(pac_project_options INTERFACE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-fsanitize=address,undefined -fno-omit-frame-pointer>)
    target_link_options(pac_project_options INTERFACE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-fsanitize=address,undefined>)
endif()

include(FetchContent)

add_subdirectory(lib)
add_subdirectory(examples)
add_subdirectory(experiments)

if(PAC_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
