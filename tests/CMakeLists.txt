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
    manifest_test.cpp
    scene_stack_test.cpp
    resource_path_test.cpp
    display_test.cpp
    geometry_test.cpp
    spritesheet_test.cpp
    animation_test.cpp
    state_store_test.cpp
    scripting_test.cpp
    room_test.cpp
    cast_test.cpp
    command_builder_test.cpp
    mover_test.cpp
    inventory_test.cpp
    camera_test.cpp
    speech_test.cpp
    text_encoding_test.cpp
    dialog_test.cpp
    save_service_test.cpp
    user_data_test.cpp
)
target_link_libraries(pac_core_tests PRIVATE pac_engine doctest::doctest pac_project_options)
# Dialog tests reach for the internal sol2-aware factory under lib/src.
target_include_directories(pac_core_tests PRIVATE ${CMAKE_SOURCE_DIR}/lib/src)
target_link_libraries(pac_core_tests PRIVATE pac_sol2)

add_test(NAME core_tests COMMAND pac_core_tests)
