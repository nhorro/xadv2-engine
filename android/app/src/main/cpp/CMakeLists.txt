cmake_minimum_required(VERSION 3.24)
project(xadv2_android_activity LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(FetchContent)

get_filename_component(PAC_ENGINE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../.." ABSOLUTE)

# Cross-compile yaml-cpp before adding the engine so pacEngineDeps reuses this
# target. Keeping it static avoids an extra native bootstrap library in the APK.
set(YAML_CPP_BUILD_CONTRIB OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(YAML_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    yaml-cpp
    GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
    GIT_TAG 0.8.0
    GIT_SHALLOW ON)
FetchContent_MakeAvailable(yaml-cpp)

# Lua intentionally has no upstream CMake build. This is the Android adapter
# for the same Lua 5.4 API consumed through the engine's pac::lua seam.
FetchContent_Declare(
    lua_source
    URL https://www.lua.org/ftp/lua-5.4.8.tar.gz
    URL_HASH SHA256=4f18ddae154e793e46eeab727c59ef1c0c0c2b744e7b94219710d76f530629ae)
FetchContent_MakeAvailable(lua_source)
set(LUA_SOURCE_DIR "${lua_source_SOURCE_DIR}/src")
add_library(lua-android STATIC
    "${LUA_SOURCE_DIR}/lapi.c"
    "${LUA_SOURCE_DIR}/lauxlib.c"
    "${LUA_SOURCE_DIR}/lbaselib.c"
    "${LUA_SOURCE_DIR}/lcode.c"
    "${LUA_SOURCE_DIR}/lcorolib.c"
    "${LUA_SOURCE_DIR}/lctype.c"
    "${LUA_SOURCE_DIR}/ldblib.c"
    "${LUA_SOURCE_DIR}/ldebug.c"
    "${LUA_SOURCE_DIR}/ldo.c"
    "${LUA_SOURCE_DIR}/ldump.c"
    "${LUA_SOURCE_DIR}/lfunc.c"
    "${LUA_SOURCE_DIR}/lgc.c"
    "${LUA_SOURCE_DIR}/linit.c"
    "${LUA_SOURCE_DIR}/liolib.c"
    "${LUA_SOURCE_DIR}/llex.c"
    "${LUA_SOURCE_DIR}/lmathlib.c"
    "${LUA_SOURCE_DIR}/lmem.c"
    "${LUA_SOURCE_DIR}/loadlib.c"
    "${LUA_SOURCE_DIR}/lobject.c"
    "${LUA_SOURCE_DIR}/lopcodes.c"
    "${LUA_SOURCE_DIR}/loslib.c"
    "${LUA_SOURCE_DIR}/lparser.c"
    "${LUA_SOURCE_DIR}/lstate.c"
    "${LUA_SOURCE_DIR}/lstring.c"
    "${LUA_SOURCE_DIR}/lstrlib.c"
    "${LUA_SOURCE_DIR}/ltable.c"
    "${LUA_SOURCE_DIR}/ltablib.c"
    "${LUA_SOURCE_DIR}/ltm.c"
    "${LUA_SOURCE_DIR}/lundump.c"
    "${LUA_SOURCE_DIR}/lutf8lib.c"
    "${LUA_SOURCE_DIR}/lvm.c"
    "${LUA_SOURCE_DIR}/lzio.c")
target_include_directories(lua-android PUBLIC "${LUA_SOURCE_DIR}")
set(PAC_LUA_INCLUDE_DIRS "${LUA_SOURCE_DIR}")
set(PAC_LUA_LIBRARIES lua-android)

# Android builds the ordinary engine target. Its dependency module selects the
# same pinned modified SFML source as desktop, with the GLES2 backend confined
# to SFML and the engine's private rendering compatibility layer.
set(PAC_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(PAC_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(PAC_INSTALL OFF CACHE BOOL "" FORCE)
set(PAC_BUILD_SHARED OFF CACHE BOOL "" FORCE)
add_subdirectory("${PAC_ENGINE_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/xadv2-engine")

# A data-only game uses the standard composition. A game with native scenes
# supplies its root CMake directory, which must expose pac::game. Thus
# desktop and Android compile one reviewable game source target.
set(PAC_ANDROID_GAME_CMAKE_DIR "" CACHE PATH
    "Root CMake directory for a game with a native composition")
if(PAC_ANDROID_GAME_CMAKE_DIR)
    if(NOT EXISTS "${PAC_ANDROID_GAME_CMAKE_DIR}/CMakeLists.txt")
        message(FATAL_ERROR
            "PAC_ANDROID_GAME_CMAKE_DIR has no CMakeLists.txt: ${PAC_ANDROID_GAME_CMAKE_DIR}")
    endif()
    add_subdirectory("${PAC_ANDROID_GAME_CMAKE_DIR}"
        "${CMAKE_CURRENT_BINARY_DIR}/android-game")
    if(NOT TARGET pac::game)
        message(FATAL_ERROR
            "The selected game must expose a pac::game CMake target")
    endif()
    set(PAC_ANDROID_DEFAULT_GAME_SOURCE "")
else()
    set(PAC_ANDROID_DEFAULT_GAME_SOURCE
        "${CMAKE_CURRENT_LIST_DIR}/default_game.cpp")
endif()

add_library(pac-android-empty-scene SHARED
    main.cpp
    ${PAC_ANDROID_DEFAULT_GAME_SOURCE})
target_include_directories(pac-android-empty-scene PRIVATE "${CMAKE_CURRENT_LIST_DIR}")
target_link_libraries(pac-android-empty-scene PRIVATE
    pac::engine
    "$<LINK_LIBRARY:WHOLE_ARCHIVE,sfml-main>"
    android
    EGL
    GLESv2
    log)
if(PAC_ANDROID_GAME_CMAKE_DIR)
    target_link_libraries(pac-android-empty-scene PRIVATE pac::game)
endif()

# SFML 2's bootstrap activity loads every module explicitly.
add_dependencies(pac-android-empty-scene
    sfml-activity
    sfml-audio
    sfml-network)
