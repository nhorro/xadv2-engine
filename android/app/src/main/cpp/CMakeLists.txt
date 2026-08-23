cmake_minimum_required(VERSION 3.24)
project(xadv2_android_empty_scene LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Keep this experiment hermetic. Desktop builds still use the system SFML 2.6
# package. Android pins the GLES2-capable SFML fork used by OpenNetBattle; the
# exact commit is intentional because upstream SFML's Android renderer remains
# tied to OpenGL ES 1 and compiles sf::Shader as an unavailable stub.
include(FetchContent)
set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)
set(SFML_BUILD_DOC OFF CACHE BOOL "" FORCE)
set(SFML_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SFML_BUILD_TEST_SUITE OFF CACHE BOOL "" FORCE)

# The GLES2 fork predates Android arm64-v8a and x86_64 support in SFML's bundled
# FreeType binaries. Fetch the official 2.6.2 source first and point CMake's
# FreeType lookup at its complete ABI set before configuring the fork. The same
# pinned source also supplies the MP3 reader imported below.
FetchContent_Declare(
    SFML_MP3_SOURCE
    GIT_REPOSITORY https://github.com/SFML/SFML.git
    GIT_TAG 5383d2b3948f805af55c9f8a4587ac72ec5981d1
    GIT_SHALLOW ON
    SOURCE_SUBDIR _source_only)
FetchContent_MakeAvailable(SFML_MP3_SOURCE)
FetchContent_GetProperties(SFML_MP3_SOURCE SOURCE_DIR sfml_mp3_source_dir)
set(FREETYPE_INCLUDE_DIR_ft2build
    "${sfml_mp3_source_dir}/extlibs/headers/freetype2" CACHE PATH "" FORCE)
set(FREETYPE_INCLUDE_DIR_freetype2
    "${sfml_mp3_source_dir}/extlibs/headers/freetype2" CACHE PATH "" FORCE)
set(FREETYPE_LIBRARY
    "${sfml_mp3_source_dir}/extlibs/libs-android/${ANDROID_ABI}/libfreetype.a"
    CACHE FILEPATH "" FORCE)
set(OPENAL_INCLUDE_DIR
    "${sfml_mp3_source_dir}/extlibs/headers/AL" CACHE PATH "" FORCE)
set(OPENAL_LIBRARY
    "${sfml_mp3_source_dir}/extlibs/libs-android/${ANDROID_ABI}/libopenal.so"
    CACHE FILEPATH "" FORCE)
set(FLAC_INCLUDE_DIR
    "${sfml_mp3_source_dir}/extlibs/headers" CACHE PATH "" FORCE)
set(FLAC_LIBRARY
    "${sfml_mp3_source_dir}/extlibs/libs-android/${ANDROID_ABI}/libFLAC.a"
    CACHE FILEPATH "" FORCE)
set(OGG_INCLUDE_DIR
    "${sfml_mp3_source_dir}/extlibs/headers" CACHE PATH "" FORCE)
set(OGG_LIBRARY
    "${sfml_mp3_source_dir}/extlibs/libs-android/${ANDROID_ABI}/libogg.a"
    CACHE FILEPATH "" FORCE)
set(VORBIS_INCLUDE_DIR
    "${sfml_mp3_source_dir}/extlibs/headers" CACHE PATH "" FORCE)
foreach(vorbis_component IN ITEMS VORBIS VORBISENC VORBISFILE)
    string(TOLOWER "${vorbis_component}" vorbis_library_name)
    set(${vorbis_component}_LIBRARY
        "${sfml_mp3_source_dir}/extlibs/libs-android/${ANDROID_ABI}/lib${vorbis_library_name}.a"
        CACHE FILEPATH "" FORCE)
endforeach()

FetchContent_Declare(
    SFML_GLES2
    GIT_REPOSITORY https://github.com/TheMaverickProgrammer/SFML_ANDROID_ES_2.git
    GIT_TAG 1ed82956eb8dfbfce09773274c95de92865ab5e1
    GIT_SHALLOW ON)
FetchContent_MakeAvailable(SFML_GLES2)
FetchContent_GetProperties(SFML_GLES2 SOURCE_DIR sfml_android_source_dir)
include("${CMAKE_CURRENT_LIST_DIR}/../../../../cmake/patch-sfml-gles2.cmake")

# Import only the official MP3 reader and its header-only minimp3 dependency;
# do not configure a second SFML.
target_sources(sfml-audio PRIVATE
    "${sfml_mp3_source_dir}/src/SFML/Audio/SoundFileReaderMp3.cpp"
    "${sfml_mp3_source_dir}/src/SFML/Audio/SoundFileReaderMp3.hpp")
target_include_directories(sfml-audio PRIVATE
    "${sfml_mp3_source_dir}/src"
    "${sfml_mp3_source_dir}/extlibs/headers/minimp3")

# Cross-compile the same YAML implementation used by the desktop engine. Keep it
# static so the APK needs no additional runtime bootstrap library.
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

# The engine targets Lua 5.4. Pin the current 5.4 maintenance release and build
# the embeddable C library directly because upstream intentionally ships a
# Makefile rather than a CMake project.
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

get_filename_component(PAC_ENGINE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../.." ABSOLUTE)

# A data-only game uses the standard built-in composition. Games that add C++
# scenes provide a tiny resource-backed bootstrap plus their ordinary game
# sources. These are build inputs, not engine or Android API branches.
set(PAC_ANDROID_GAME_BOOTSTRAP "" CACHE FILEPATH
    "Source implementing pac::android::run_game for the selected game")
set(PAC_ANDROID_GAME_SOURCES "" CACHE STRING
    "Semicolon-separated game C++ sources linked into the Android activity")
set(PAC_ANDROID_GAME_INCLUDE_DIRS "" CACHE STRING
    "Semicolon-separated include directories for game C++ sources")
set(PAC_ANDROID_VERSION_NAME "" CACHE STRING
    "Selected external game's display version")
if(NOT PAC_ANDROID_GAME_BOOTSTRAP)
    set(PAC_ANDROID_GAME_BOOTSTRAP
        "${CMAKE_CURRENT_LIST_DIR}/standard_game_bootstrap.cpp")
endif()
if(NOT EXISTS "${PAC_ANDROID_GAME_BOOTSTRAP}")
    message(FATAL_ERROR
        "PAC_ANDROID_GAME_BOOTSTRAP does not exist: ${PAC_ANDROID_GAME_BOOTSTRAP}")
endif()

# Cross-compile the complete real engine. CONFIGURE_DEPENDS keeps this Android
# experiment from maintaining a second hand-written copy of lib/CMakeLists.txt's
# source inventory while the portability work is still evolving.
file(GLOB_RECURSE PAC_ENGINE_SOURCES CONFIGURE_DEPENDS
    "${PAC_ENGINE_ROOT}/lib/src/*.cpp")
add_library(pac-android-empty-scene SHARED
    main.cpp
    "${PAC_ANDROID_GAME_BOOTSTRAP}"
    ${PAC_ANDROID_GAME_SOURCES}
    ${PAC_ENGINE_SOURCES})

target_include_directories(pac-android-empty-scene PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}"
    "${PAC_ENGINE_ROOT}/lib/include"
    "${PAC_ENGINE_ROOT}/lib/src"
    "${PAC_ENGINE_ROOT}/third_party/sol2"
    ${PAC_ANDROID_GAME_INCLUDE_DIRS})
if(PAC_ANDROID_VERSION_NAME)
    target_compile_definitions(pac-android-empty-scene PRIVATE
        FDC_VERSION="${PAC_ANDROID_VERSION_NAME}")
endif()

# sfml-main owns ANativeActivity_onCreate. Nothing references that symbol at
# normal link time, so its object file must be retained as a whole archive.
target_link_libraries(pac-android-empty-scene PRIVATE
    sfml-audio
    sfml-graphics
    lua-android
    yaml-cpp::yaml-cpp
    "$<LINK_LIBRARY:WHOLE_ARCHIVE,sfml-main>"
    android
    EGL
    GLESv2
    log)

# SFML 2's bootstrap activity loads every SFML module explicitly. Build all of
# them even though this first scene only calls Graphics, Window, and System.
add_dependencies(pac-android-empty-scene
    sfml-activity
    sfml-audio
    sfml-network)
