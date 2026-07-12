# The only example that compiles game code of its own. Note what it links:
#
#   pac_engine  — the engine, as any game does
#   pac_sol2    — sol2 + Lua headers, needed ONLY because this game installs its
#                 own Lua functions (engine/core/scripting_sol.hpp / sol::state)
#   yaml-cpp    — needed ONLY because it parses its own data file
#
# A game with no custom C++ links none of these but pac_engine.
add_executable(pac_example_06_cpp_scene
    main.cpp
    src/field_notes.cpp
)
target_include_directories(pac_example_06_cpp_scene PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

if(TARGET yaml-cpp::yaml-cpp)
    set(PAC_EXAMPLE_YAMLCPP yaml-cpp::yaml-cpp)
else()
    set(PAC_EXAMPLE_YAMLCPP yaml-cpp)
endif()

target_link_libraries(pac_example_06_cpp_scene
    PRIVATE pac_engine pac_sol2 ${PAC_EXAMPLE_YAMLCPP} pac_project_options)
