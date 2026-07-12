# The examples: one per engine feature, each the smallest game that shows it.
#
# They are not a game and not a test fixture — they are the documentation's
# worked code. Each builds a `pac_example_<name>` executable and runs from the
# repo root against its own data/ directory:
#
#   ./build/examples/01_hello_room/pac_example_01_hello_room
#
# CI smokes every one of them headless (--frames N), so an engine change that
# breaks an example breaks the build.
add_subdirectory(01_hello_room)
add_subdirectory(02_scumm_inventory)
add_subdirectory(03_dialog_npc)
add_subdirectory(04_cutscene)
add_subdirectory(05_closeup)
add_subdirectory(06_cpp_scene)
