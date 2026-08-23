# One SFML source/API for every runtime platform.
#
# The engine deliberately owns this dependency instead of accepting whichever
# system/vcpkg SFML happens to be installed. Desktop builds use the fork's
# desktop OpenGL backend; Android uses its GLES2 backend. Platform corrections
# remain inside this dependency layer and never change the game API.
include_guard(GLOBAL)

if(TARGET sfml-graphics)
    return()
endif()

include(FetchContent)

# SFML 2 selects its library type through the generic BUILD_SHARED_LIBS
# variable. Keep that choice local to dependency acquisition so including the
# engine cannot change the default library type of the game or its other
# dependencies.
function(pac_acquire_modified_sfml)

set(PAC_SFML_FORK_REPOSITORY
    "https://github.com/TheMaverickProgrammer/SFML_ANDROID_ES_2.git"
    CACHE STRING "Modified SFML repository used by every platform")
set(PAC_SFML_FORK_REVISION
    "1ed82956eb8dfbfce09773274c95de92865ab5e1"
    CACHE STRING "Pinned modified SFML revision used by every platform")
set(PAC_SFML_REFERENCE_REVISION
    "5383d2b3948f805af55c9f8a4587ac72ec5981d1"
    CACHE STRING "Pinned official SFML 2.6.2 source used for dependencies/backports")

set(SFML_BUILD_DOC OFF CACHE BOOL "" FORCE)
set(SFML_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SFML_BUILD_TEST_SUITE OFF CACHE BOOL "" FORCE)
set(SFML_BUILD_NETWORK ON CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS ON)

# The reference source supplies the current Android extlibs and the MP3 reader.
# It is source-only: the runtime always links the modified fork below.
FetchContent_Declare(
    pac_sfml_reference
    GIT_REPOSITORY https://github.com/SFML/SFML.git
    GIT_TAG ${PAC_SFML_REFERENCE_REVISION}
    GIT_SHALLOW ON
    SOURCE_SUBDIR _source_only)
FetchContent_MakeAvailable(pac_sfml_reference)
FetchContent_GetProperties(pac_sfml_reference SOURCE_DIR sfml_mp3_source_dir)

if(ANDROID)
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
elseif(WIN32)
    # The fork carries the matching MSVC dependency binaries. Keeping those
    # avoids reintroducing vcpkg's independent SFML port or version selection.
    set(SFML_USE_SYSTEM_DEPS OFF CACHE BOOL "" FORCE)
else()
    # Keep codec/window dependencies native to the host while pinning the SFML
    # implementation itself. This avoids shipping stale fork-bundled desktop
    # binaries and keeps Unix package discovery conventional.
    set(SFML_USE_SYSTEM_DEPS ON CACHE BOOL "" FORCE)
endif()

FetchContent_Declare(
    pac_modified_sfml
    GIT_REPOSITORY ${PAC_SFML_FORK_REPOSITORY}
    GIT_TAG ${PAC_SFML_FORK_REVISION}
    GIT_SHALLOW ON)
FetchContent_MakeAvailable(pac_modified_sfml)
FetchContent_GetProperties(pac_modified_sfml SOURCE_DIR sfml_android_source_dir)

# Source-tree builds keep the patches under android/cmake. Installed engine
# packages place them beside this module.
get_filename_component(pac_engine_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
if(EXISTS "${pac_engine_root}/android/cmake/patch-sfml-gles2.cmake")
    set(pac_sfml_patch_dir "${pac_engine_root}/android/cmake")
else()
    set(pac_sfml_patch_dir "${CMAKE_CURRENT_LIST_DIR}")
endif()
include("${pac_sfml_patch_dir}/patch-sfml-gles2.cmake")

# Import the official 2.6 MP3 reader into the single modified SFML audio target.
target_sources(sfml-audio PRIVATE
    "${sfml_mp3_source_dir}/src/SFML/Audio/SoundFileReaderMp3.cpp"
    "${sfml_mp3_source_dir}/src/SFML/Audio/SoundFileReaderMp3.hpp")
target_include_directories(sfml-audio PRIVATE
    "${sfml_mp3_source_dir}/src"
    "${sfml_mp3_source_dir}/extlibs/headers/minimp3")

message(STATUS
    "pac_engine: modified SFML ${PAC_SFML_FORK_REVISION} (${CMAKE_SYSTEM_NAME})")
endfunction()

pac_acquire_modified_sfml()
