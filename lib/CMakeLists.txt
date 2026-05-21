# Engine library: pac_engine. Owns pac::core (harness) and pac::pnc (genre).
find_package(SFML 2.6 COMPONENTS graphics window system REQUIRED)
find_package(yaml-cpp REQUIRED)

add_library(pac_engine STATIC
    src/core/diagnostics.cpp
    src/core/settings.cpp
    src/core/scene_params.cpp
    src/core/resource_source.cpp
    src/core/strings.cpp
    src/core/manifest.cpp
    src/core/display.cpp
    src/core/scene_factory.cpp
    src/core/scene_manager.cpp
    src/core/application.cpp
    src/pnc/blank_scene.cpp
    src/pnc/title_screen.cpp
    src/pnc/settings_scene.cpp
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
target_link_libraries(pac_engine
    PUBLIC sfml-graphics sfml-window sfml-system pac_project_options
    PRIVATE ${PAC_YAMLCPP})
