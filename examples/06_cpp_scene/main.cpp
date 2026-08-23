// 06_cpp_scene — a game that adds a scene type of its own.
//
// The other examples' main() is one line (`run_game_main`), because they need no
// C++ at all. This one delegates to a game-owned composition shared by desktop
// and resource-backed platforms; that composition adds its scene type and Lua
// configuration hook on top of the engine's built-ins.
#include "engine/core/game.hpp"

#include <string>

int main(int argc, char** argv) {
    pac::core::RunOptions opts;
    const std::string manifest =
        pac::core::parse_run_options(argc, argv, opts, "examples/06_cpp_scene/data/game.yaml");

    std::unique_ptr<pac::core::Game> game = pac::game::create();
    return pac::core::run_game(manifest, *game, opts);
}
