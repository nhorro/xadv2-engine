# Engine library: pac_engine. Owns pac::core (harness), pac::geom, pac::gfx, and
# pac::pnc (genre).
find_package(SFML 2.6 COMPONENTS graphics window audio system REQUIRED)
find_package(yaml-cpp REQUIRED)

# Lua 5.4, behind a single `pac_lua` INTERFACE seam so the rest of the build never
# names a discovery-specific target. Two discovery paths:
#  - Unix package managers: pkg-config. Distros disagree on the module name
#    (Debian/Ubuntu `lua5.4`, Arch `lua-5.4`, Fedora/RHEL plain `lua` at 5.4.x), so
#    we probe in order. GLOBAL on the IMPORTED_TARGET so `tests/` (a sibling of
#    `lib/`) can link it transitively via pac_sol2 below.
#  - vcpkg / Windows-MSVC: no pkg-config, so fall back to CMake's FindLua module,
#    which locates vcpkg's lua (lua.lib + lua.h) and yields LUA_INCLUDE_DIR /
#    LUA_LIBRARIES variables (it exports no imported target).
add_library(pac_lua INTERFACE)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(LUA IMPORTED_TARGET GLOBAL lua5.4)
    if(NOT LUA_FOUND)
        pkg_check_modules(LUA IMPORTED_TARGET GLOBAL lua-5.4)
    endif()
    if(NOT LUA_FOUND)
        pkg_check_modules(LUA IMPORTED_TARGET GLOBAL lua>=5.4)
    endif()
endif()
if(TARGET PkgConfig::LUA)
    target_link_libraries(pac_lua INTERFACE PkgConfig::LUA)
    message(STATUS "lib: Lua via pkg-config (${LUA_LIBRARIES})")
else()
    find_package(Lua 5.4 REQUIRED)
    target_include_directories(pac_lua INTERFACE ${LUA_INCLUDE_DIR})
    target_link_libraries(pac_lua INTERFACE ${LUA_LIBRARIES})
    message(STATUS "lib: Lua via FindLua (${LUA_LIBRARIES})")
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
    target_link_libraries(pac_sol2 INTERFACE pac_lua)
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
    src/core/settings_store.cpp
    src/core/scene_params.cpp
    src/core/resource_source.cpp
    src/core/resource_cache.cpp
    src/core/pack_format.cpp
    src/core/pack_resource_source.cpp
    src/core/strings.cpp
    src/core/localization.cpp
    src/core/text_layout.cpp
    src/core/manifest.cpp
    src/core/display.cpp
    src/core/audio.cpp
    src/core/scene_factory.cpp
    src/core/scene_manager.cpp
    src/core/screen_fade.cpp
    src/core/state_store.cpp
    src/core/facts.cpp
    src/core/scripting.cpp
    src/core/lua_api.cpp
    src/core/user_data.cpp
    src/core/save_service.cpp
    src/core/thumbnail.cpp
    src/core/profiler.cpp
    src/core/render_stats.cpp
    src/core/application.cpp
    src/geom/geometry.cpp
    src/gfx/spritesheet.cpp
    src/gfx/animation.cpp
    src/gfx/sequence_player.cpp
    src/gfx/animated_sprite.cpp
    src/gfx/shader_effect.cpp
    src/gfx/shader_chain.cpp
    src/pnc/blank_scene.cpp
    src/pnc/shader_yaml.cpp
    src/pnc/title_screen.cpp
    src/pnc/settings_scene.cpp
    src/pnc/save_load_scene.cpp
    src/pnc/story_text_scene.cpp
    src/pnc/cutscene.cpp
    src/pnc/cutscene_scene.cpp
    src/pnc/room.cpp
    src/pnc/cast.cpp
    src/pnc/mover.cpp
    src/pnc/avatar.cpp
    src/pnc/approach_follow.cpp
    src/pnc/speech_manager.cpp
    src/pnc/room_runtime.cpp
    src/pnc/room_renderer.cpp
    src/pnc/debug_overlay.cpp
    src/pnc/dev_actions.cpp
    src/pnc/command.cpp
    src/pnc/command_builder.cpp
    src/pnc/command_controller.cpp
    src/pnc/inventory.cpp
    src/pnc/camera.cpp
    src/pnc/scumm_panel_config.cpp
    src/pnc/scumm_panel.cpp
    src/pnc/pause_overlay.cpp
    src/pnc/dialog.cpp
    src/pnc/closeup.cpp
    src/pnc/closeup_runtime.cpp
    src/pnc/room_scene.cpp
    src/pnc/closeup_scene.cpp
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
    PRIVATE ${PAC_YAMLCPP} ${PAC_SOL2} pac_lua)
