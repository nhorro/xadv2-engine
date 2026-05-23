# Engine library: pac_engine. Owns pac::core (harness), pac::geom, pac::gfx, and
# pac::pnc (genre).
find_package(SFML 2.6 COMPONENTS graphics window audio system REQUIRED)
find_package(yaml-cpp REQUIRED)
find_package(PkgConfig REQUIRED)
# Distros disagree on the pkg-config name for Lua 5.4: Debian/Ubuntu ship
# `lua5.4`, Arch ships `lua-5.4`, Fedora/RHEL ship plain `lua` with Version: 5.4.x.
# GLOBAL on the IMPORTED_TARGET so `tests/` (a sibling of `lib/`) can also link
# `PkgConfig::LUA` via pac_sol2's transitive INTERFACE dependency below.
pkg_check_modules(LUA IMPORTED_TARGET GLOBAL lua5.4)
if(NOT LUA_FOUND)
    pkg_check_modules(LUA IMPORTED_TARGET GLOBAL lua-5.4)
endif()
if(NOT LUA_FOUND)
    pkg_check_modules(LUA REQUIRED IMPORTED_TARGET GLOBAL lua>=5.4)
endif()

# sol2 (header-only). The vendored single header is preferred (github.com git is
# unreachable here); delete third_party/sol2 to fall back to FetchContent.
set(PAC_SOL2_LOCAL ${CMAKE_SOURCE_DIR}/third_party/sol2/sol/sol.hpp)
if(EXISTS ${PAC_SOL2_LOCAL})
    add_library(pac_sol2 INTERFACE)
    target_include_directories(pac_sol2 INTERFACE ${CMAKE_SOURCE_DIR}/third_party/sol2)
    target_compile_definitions(pac_sol2 INTERFACE SOL_ALL_SAFETIES_ON=1)
    # sol2 #include <lua.h> internally, so anyone linking pac_sol2 transitively
    # needs Lua's headers/libs. Without this, targets that mention pac_sol2 but
    # not pac_engine (the test executable's dialog_test in particular) fail to
    # find lua.h on distros that put it under a versioned subdir like
    # /usr/include/lua5.4 (Debian/Ubuntu). Fedora has lua.h directly in
    # /usr/include, which is in the default search path — hence the local build
    # passed but CI on Ubuntu didn't.
    target_link_libraries(pac_sol2 INTERFACE PkgConfig::LUA)
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
    src/core/load_error.cpp
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
    src/core/screen_fade.cpp
    src/core/state_store.cpp
    src/core/scripting.cpp
    src/core/lua_api.cpp
    src/core/user_data.cpp
    src/core/save_service.cpp
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
    src/pnc/mover.cpp
    src/pnc/avatar.cpp
    src/pnc/speech_manager.cpp
    src/pnc/room_runtime.cpp
    src/pnc/room_renderer.cpp
    src/pnc/debug_overlay.cpp
    src/pnc/dev_actions.cpp
    src/pnc/command.cpp
    src/pnc/command_builder.cpp
    src/pnc/inventory.cpp
    src/pnc/camera.cpp
    src/pnc/scumm_panel.cpp
    src/pnc/dialog.cpp
    src/pnc/room_scene.cpp
    src/pnc/builtin_scenes.cpp
    src/pnc/game_app.cpp
)
add_library(pac::engine ALIAS pac_engine)

target_include_directories(pac_engine
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

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
