# {{title}} — a game built on the Extraordinary Adventures Engine v2.
#
# This is a STANDALONE project: it lives in its own repository and consumes the
# engine as a library. There are two ways to do that, and you will want both at
# different times:
#
#   1. SOURCE mode — point at an engine checkout. Use this while the engine is
#      still moving: you can fix an engine bug from inside your game's build,
#      rebuild, and keep going.
#
#          cmake -S . -B build -DXADV2_ENGINE_DIR=~/workspace/xadv2-engine
#
#   2. INSTALLED mode — link an engine that has been installed somewhere. Use
#      this once the engine is stable enough that you don't want to rebuild it.
#
#          cmake --install <engine-build> --prefix ~/.local   # once, in the engine
#          cmake -S . -B build -DCMAKE_PREFIX_PATH=~/.local   # here
#
# Nothing else in this file changes between the two.
cmake_minimum_required(VERSION 3.16)
project({{short_name}} VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

set(XADV2_ENGINE_DIR "" CACHE PATH
    "Path to an xadv2-engine checkout. Empty = use an installed engine (find_package).")

if(XADV2_ENGINE_DIR)
    message(STATUS "{{short_name}}: building the engine from source at ${XADV2_ENGINE_DIR}")
    # The engine contributes only its library here: its examples, tests and
    # experiments stay off, because it knows it is not the top-level project.
    add_subdirectory(${XADV2_ENGINE_DIR} xadv2-engine)
else()
    find_package(pac_engine 0.9 CONFIG REQUIRED)
    message(STATUS "{{short_name}}: using the installed engine ${pac_engine_VERSION}")
endif()

add_executable({{short_name}} main.cpp)
target_link_libraries({{short_name}} PRIVATE pac::engine)

# A game that adds C++ scenes of its own (a journal, a map, a minigame — anything
# whose *interaction model* the engine doesn't already have) also needs sol2, to
# install its own Lua functions. Add the sources to the executable above and link:
#
#   target_link_libraries({{short_name}} PRIVATE pac::engine pac::sol2)
#
# See the engine's examples/06_cpp_scene for the whole pattern. Until you need it,
# your game is data: YAML for what exists, Lua for what happens.
