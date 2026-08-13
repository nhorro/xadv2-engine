# Engine library: pac_engine (exported as pac::engine). Owns pac::core (harness),
# pac::geom, pac::gfx, and pac::pnc (genre).
#
# Two ways a game consumes this:
#   * source     — add_subdirectory(<engine>) / FetchContent, then link pac::engine.
#   * installed  — cmake --install, then find_package(pac_engine) -> pac::engine.
# Both must see the same targets, which is why dependency discovery lives in
# cmake/pacEngineDeps.cmake and is shared with the generated package config.
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(pacEngineDeps)   # -> SFML, ${PAC_YAMLCPP}, PAC_LUA_INCLUDE_DIRS/LIBRARIES

# --- dependency seams ---------------------------------------------------------
# The rest of the build never names a discovery-specific target: it links these.

add_library(pac_lua INTERFACE)
# Lua's location is machine-specific — pkg-config on Unix, FindLua on Windows,
# where vcpkg puts its headers and .lib INSIDE the engine's build tree. Neither can
# be baked into an exported target: CMake flatly rejects a build-directory include
# path in an install interface (this is what broke the Windows build), and a
# consumer's Lua may live somewhere else entirely anyway.
#
# So: the build tree gets the real paths here, behind $<BUILD_INTERFACE:>, and the
# installed package re-attaches them in pac_engineConfig.cmake from its own
# discovery run. The two must stay in sync — both go through cmake/pacEngineDeps.cmake.
if(PAC_LUA_INCLUDE_DIRS)
    target_include_directories(pac_lua INTERFACE $<BUILD_INTERFACE:${PAC_LUA_INCLUDE_DIRS}>)
endif()
target_link_libraries(pac_lua INTERFACE $<BUILD_INTERFACE:${PAC_LUA_LIBRARIES}>)
# Without EXPORT_NAME, the namespaced export would be `pac::pac_lua`.
set_target_properties(pac_lua PROPERTIES EXPORT_NAME lua)
add_library(pac::lua ALIAS pac_lua)

# sol2 (header-only). The vendored single header is preferred; delete
# third_party/sol2 to fall back to FetchContent.
#
# PROJECT_SOURCE_DIR, not CMAKE_SOURCE_DIR: when a game does
# add_subdirectory(<engine>), CMAKE_SOURCE_DIR is the GAME's root, and looking for
# third_party/sol2 there silently misses the vendored copy and FetchContent's sol2
# from the network instead.
#
# sol2 is PRIVATE to the engine's scripting TUs and never appears in a public
# engine header — with one deliberate exception: `engine/core/scripting_sol.hpp`,
# the opt-in seam a GAME includes when it wants to add its own Lua functions. That
# is why sol2 ships with the package and is exported as pac::sol2: a game with
# custom C++ scenes links it, a data-only game never mentions it.
set(PAC_SOL2_LOCAL ${PROJECT_SOURCE_DIR}/third_party/sol2/sol/sol.hpp)
if(EXISTS ${PAC_SOL2_LOCAL})
    add_library(pac_sol2 INTERFACE)
    target_include_directories(pac_sol2 INTERFACE
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/third_party/sol2>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/pac_engine/third_party>)
    target_compile_definitions(pac_sol2 INTERFACE SOL_ALL_SAFETIES_ON=1)
    # sol2 #includes <lua.h> internally, so anyone linking pac_sol2 transitively
    # needs Lua's headers/libs. Without this, targets that mention pac_sol2 but not
    # pac_engine fail to find lua.h on distros that put it under a versioned subdir
    # (Debian/Ubuntu /usr/include/lua5.4); Fedora has it in /usr/include, which is
    # already on the default search path — hence "works here, fails in CI".
    target_link_libraries(pac_sol2 INTERFACE pac_lua)
    set_target_properties(pac_sol2 PROPERTIES EXPORT_NAME sol2)
    add_library(pac::sol2 ALIAS pac_sol2)
    set(PAC_SOL2 pac_sol2)
    set(PAC_SOL2_VENDORED TRUE)
    message(STATUS "lib: using vendored sol2 single header (third_party/sol2)")
else()
    FetchContent_Declare(sol2
        GIT_REPOSITORY https://github.com/ThePhD/sol2.git
        GIT_TAG v3.3.0)
    FetchContent_MakeAvailable(sol2)
    set(PAC_SOL2 sol2::sol2)
    set(PAC_SOL2_VENDORED FALSE)
endif()

# --- the engine ---------------------------------------------------------------

if(PAC_BUILD_SHARED)
    if(MSVC)
        # A Windows DLL exports nothing unless every public symbol carries
        # __declspec(dllexport). The engine has no export macro yet, so a shared
        # build would link and then hand the consumer an import library with no
        # symbols in it. Fail here rather than mysteriously at their link step.
        message(FATAL_ERROR
            "PAC_BUILD_SHARED is not supported on MSVC yet: the engine needs a generated "
            "export header + PAC_ENGINE_API on its public types first. Build static "
            "(-DPAC_BUILD_SHARED=OFF, the default) on Windows.")
    endif()
    set(PAC_ENGINE_LIBRARY_TYPE SHARED)
else()
    set(PAC_ENGINE_LIBRARY_TYPE STATIC)
endif()

add_library(pac_engine ${PAC_ENGINE_LIBRARY_TYPE}
    src/core/diagnostics.cpp
    src/core/load_error.cpp
    src/core/settings.cpp
    src/core/settings_store.cpp
    src/core/system_language.cpp
    src/core/speech_config.cpp
    src/core/scene_params.cpp
    src/core/resource_source.cpp
    src/core/resource_cache.cpp
    src/core/pack_format.cpp
    src/core/pack_resource_source.cpp
    src/core/strings.cpp
    src/core/localization.cpp
    src/core/translation_catalog.cpp
    src/core/text_id.cpp
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
    src/gfx/composite_sprite.cpp
    src/gfx/visual_sprite.cpp
    src/gfx/shader_effect.cpp
    src/gfx/shader_chain.cpp
    src/pnc/blank_scene.cpp
    src/pnc/shader_yaml.cpp
    src/pnc/ui_sound_cues.cpp
    src/pnc/title_screen.cpp
    src/pnc/confirmation_scene.cpp
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
    src/pnc/room_lighting.cpp
    src/pnc/room_tuning_overlay.cpp
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
    src/pnc/case_resolution.cpp
    src/pnc/case_resolution_runtime.cpp
    src/pnc/closeup_runtime.cpp
    src/pnc/room_scene.cpp
    src/pnc/closeup_scene.cpp
    src/pnc/case_resolution_scene.cpp
    src/pnc/builtin_scenes.cpp
    src/pnc/game_app.cpp
)
add_library(pac::engine ALIAS pac_engine)
set_target_properties(pac_engine PROPERTIES
    EXPORT_NAME engine
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR})

# C++20 is part of the engine's contract, not a project-local convenience: a
# consumer compiling against these headers needs it too.
target_compile_features(pac_engine PUBLIC cxx_std_20)

target_include_directories(pac_engine
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# SFML types appear in public headers, so SFML is PUBLIC; yaml-cpp, sol2 and Lua
# are implementation details of the loaders and scripting TUs, so they stay
# PRIVATE. (For a STATIC engine, CMake still records the private deps in the link
# interface as $<LINK_ONLY:...>, which is why the package config has to be able to
# find them again — see cmake/pacEngineDeps.cmake.)
#
# pac_sanitizers is PUBLIC on purpose: a sanitized engine must not be linked into
# an unsanitized game. pac_warnings is wrapped in $<BUILD_INTERFACE:> on purpose:
# a STATIC library records even its PRIVATE deps in the link interface (as
# $<LINK_ONLY:...>), so a plain PRIVATE link would drag the engine's warning flags
# into the export — and into every consumer's own compile line.
target_link_libraries(pac_engine
    PUBLIC sfml-graphics sfml-window sfml-audio sfml-system pac_sanitizers
    PRIVATE ${PAC_YAMLCPP} ${PAC_SOL2} pac_lua $<BUILD_INTERFACE:pac_warnings>)

# --- install + export ---------------------------------------------------------

if(PAC_INSTALL)
    if(NOT PAC_SOL2_VENDORED)
        message(FATAL_ERROR
            "PAC_INSTALL needs the vendored sol2 (third_party/sol2): a FetchContent'd "
            "sol2::sol2 cannot be exported with the package.")
    endif()

    install(TARGETS pac_engine pac_lua pac_sol2 pac_sanitizers
            EXPORT pacEngineTargets
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
            INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

    install(DIRECTORY include/engine
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
            FILES_MATCHING PATTERN "*.hpp")

    # sol2's headers ship with the package: engine/core/scripting_sol.hpp includes
    # <sol/sol.hpp>, so a game that extends the engine in C++ must be able to find
    # it without vendoring sol2 itself.
    install(DIRECTORY ${PROJECT_SOURCE_DIR}/third_party/sol2/sol
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/pac_engine/third_party)

    install(EXPORT pacEngineTargets
            FILE pacEngineTargets.cmake
            NAMESPACE pac::
            DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/pac_engine)

    configure_package_config_file(
        ${PROJECT_SOURCE_DIR}/cmake/pac_engineConfig.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/pac_engineConfig.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/pac_engine)

    write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/pac_engineConfigVersion.cmake
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMinorVersion)

    install(FILES
            ${CMAKE_CURRENT_BINARY_DIR}/pac_engineConfig.cmake
            ${CMAKE_CURRENT_BINARY_DIR}/pac_engineConfigVersion.cmake
            ${PROJECT_SOURCE_DIR}/cmake/pacEngineDeps.cmake
            DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/pac_engine)
endif()
