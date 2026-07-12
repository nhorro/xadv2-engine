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

# The engine finds yaml-cpp for itself, but as a PRIVATE dependency, in lib/'s
# directory scope — and an imported target is only visible in the directory that
# created it and below. So it is NOT visible here, and would not be visible to a
# real game in another repository either. A game that parses its own YAML finds
# yaml-cpp itself, exactly like this. (Getting this wrong linked the literal name
# `yaml-cpp`, which happens to resolve to a system library on Linux and fails on
# Windows with "cannot open input file 'yaml-cpp.lib'".)
find_package(yaml-cpp REQUIRED)
if(TARGET yaml-cpp::yaml-cpp)
    set(PAC_EXAMPLE_YAMLCPP yaml-cpp::yaml-cpp)
else()
    set(PAC_EXAMPLE_YAMLCPP yaml-cpp)   # yaml-cpp < 0.8 exports the bare name
endif()

target_link_libraries(pac_example_06_cpp_scene
    PRIVATE pac::engine pac::sol2 ${PAC_EXAMPLE_YAMLCPP} pac_project_options)
