# Engine library: pac_engine. Owns pac::core (harness), pac::geom, pac::gfx, and
# pac::pnc (genre).
find_package(SFML 2.6 COMPONENTS graphics window audio system REQUIRED)
find_package(yaml-cpp REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LUA REQUIRED IMPORTED_TARGET lua5.4)

# sol2 (header-only). The vendored single header is preferred (github.com git is
# unreachable here); delete third_party/sol2 to fall back to FetchContent.
set(PAC_SOL2_LOCAL ${CMAKE_SOURCE_DIR}/third_party/sol2/sol/sol.hpp)
if(EXISTS ${PAC_SOL2_LOCAL})
    add_library(pac_sol2 INTERFACE)
    target_include_directories(pac_sol2 INTERFACE ${CMAKE_SOURCE_DIR}/third_party/sol2)
    target_compile_definitions(pac_sol2 INTERFACE SOL_ALL_SAFETIES_ON=1)
    set(PAC_SOL2 pac_sol2)
    message(STATUS "lib: using vendored sol2 single header (third_party/sol2)")
else()
    FetchContent_Declare(sol2
        GIT_REPOSITORY https://github.com/ThePhD/sol2.git
        GIT_TAG v3.3.0)
    FetchContent_MakeAvailable(sol2)
    set(PAC_SOL2 sol2::sol2)
endif()

add_library(pac_engine STATIC
    src/core/diagnostics.cpp
    src/core/settings.cpp
    src/core/scene_params.cpp
    src/core/resource_source.cpp
    src/core/resource_cache.cpp
    src/core/strings.cpp
    src/core/manifest.cpp
    src/core/display.cpp
    src/core/audio.cpp
    src/core/scene_factory.cpp
    src/core/scene_manager.cpp
    src/core/state_store.cpp
    src/core/scripting.cpp
    src/core/lua_api.cpp
    src/core/application.cpp
    src/geom/geometry.cpp
    src/gfx/spritesheet.cpp
    src/gfx/animation.cpp
    src/gfx/sequence_player.cpp
    src/gfx/animated_sprite.cpp
    src/pnc/blank_scene.cpp
    src/pnc/title_screen.cpp
    src/pnc/settings_scene.cpp
    src/pnc/story_text_scene.cpp
    src/pnc/room.cpp
    src/pnc/cast.cpp
    src/pnc/avatar.cpp
    src/pnc/speech_manager.cpp
    src/pnc/room_runtime.cpp
    src/pnc/room_renderer.cpp
    src/pnc/room_scene.cpp
    src/pnc/builtin_scenes.cpp
    src/pnc/game_app.cpp
)
add_library(pac::engine ALIAS pac_engine)

target_include_directories(pac_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# yaml-cpp 0.8 exports the namespaced target; older packages use the bare name.
if(TARGET yaml-cpp::yaml-cpp)
    set(PAC_YAMLCPP yaml-cpp::yaml-cpp)
else()
    set(PAC_YAMLCPP yaml-cpp)
endif()

# SFML types appear in public headers, so SFML is PUBLIC; yaml-cpp is an
# implementation detail of the loaders, so it stays PRIVATE.
# sol2 + Lua are implementation details of the scripting TUs, so they stay PRIVATE
# (sol2 never appears in a public engine header).
target_link_libraries(pac_engine
    PUBLIC sfml-graphics sfml-window sfml-audio sfml-system pac_project_options
    PRIVATE ${PAC_YAMLCPP} ${PAC_SOL2} PkgConfig::LUA)
