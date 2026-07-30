# Headless unit tests (doctest + CTest).
#
# The design pulls doctest (header-only) via FetchContent at a pinned tag. Because
# github.com (git) is unreachable here, the genuine doctest v2.4.11 single header
# is vendored under _vendor/ instead, and preferred when present. Delete _vendor/
# to fall back to FetchContent once git access is available.
set(PAC_DOCTEST_LOCAL ${CMAKE_CURRENT_SOURCE_DIR}/_vendor/doctest/doctest.h)
if(EXISTS ${PAC_DOCTEST_LOCAL})
    add_library(pac_doctest INTERFACE)
    target_include_directories(pac_doctest INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/_vendor)
    add_library(doctest::doctest ALIAS pac_doctest)
    message(STATUS "tests: using vendored doctest v2.4.11 single header "
                   "(remove tests/_vendor to use FetchContent)")
else()
    FetchContent_Declare(doctest
        GIT_REPOSITORY https://github.com/doctest/doctest.git
        GIT_TAG v2.4.11)
    FetchContent_MakeAvailable(doctest)
endif()

add_executable(pac_core_tests
    test_main.cpp
    load_error_test.cpp
    manifest_test.cpp
    strings_test.cpp
    localization_test.cpp
    text_layout_test.cpp
    settings_store_test.cpp
    scene_stack_test.cpp
    screen_fade_test.cpp
    resource_path_test.cpp
    pack_resource_source_test.cpp
    display_test.cpp
    audio_test.cpp
    cursor_test.cpp
    geometry_test.cpp
    spritesheet_test.cpp
    animation_test.cpp
    avatar_test.cpp
    shader_effect_test.cpp
    state_store_test.cpp
    facts_test.cpp
    scripting_test.cpp
    room_test.cpp
    cast_test.cpp
    closeup_test.cpp
    case_resolution_test.cpp
    closeup_runtime_test.cpp
    cutscene_test.cpp
    command_builder_test.cpp
    command_controller_test.cpp
    scumm_panel_config_test.cpp
    dialog_options_layout_test.cpp
    mover_test.cpp
    approach_follow_test.cpp
    inventory_test.cpp
    camera_test.cpp
    debug_overlay_test.cpp
    dev_actions_test.cpp
    speech_test.cpp
    text_encoding_test.cpp
    dialog_test.cpp
    save_service_test.cpp
    user_data_test.cpp
    profiler_test.cpp
    application_hook_test.cpp
    pause_overlay_test.cpp
)
target_link_libraries(pac_core_tests PRIVATE pac_engine doctest::doctest pac_project_options)
# Dialog tests reach for the internal sol2-aware factory under lib/src.
target_include_directories(pac_core_tests PRIVATE ${CMAKE_SOURCE_DIR}/lib/src)
target_link_libraries(pac_core_tests PRIVATE pac_sol2)

add_test(NAME core_tests COMMAND pac_core_tests)

# Every example is smoke-run: it must load its manifest, build its scenes, and
# render frames without erroring. An engine change that breaks an example's data
# therefore breaks the test suite — which is the point of keeping the examples
# in-tree rather than letting them rot.
#
# These are the ONLY tests that open a window (SFML needs a real GL context, even
# for `--frames 5`), so they carry the `gui` label and the rest of the suite stays
# purely headless:
#
#   ctest -LE gui                 # headless — the default CI job
#   xvfb-run -a ctest -L gui      # the example smoke runs
#
# They run from the repo root, since their default manifest paths are repo-relative.
if(TARGET pac_example_01_hello_room)
    foreach(example
        01_hello_room
        02_scumm_inventory
        03_dialog_npc
        04_cutscene
        05_closeup
        06_cpp_scene)
        add_test(NAME example_${example}
                 COMMAND pac_example_${example} --frames 5
                 WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
        set_tests_properties(example_${example} PROPERTIES LABELS gui)
    endforeach()
endif()
