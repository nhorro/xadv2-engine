#include "engine/core/game.hpp"
#include "engine/pnc/builtin_scenes.hpp"

namespace pac::game {

std::unique_ptr<pac::core::Game> create() {
    auto game = std::make_unique<pac::core::Game>();
    pac::pnc::register_builtin_scenes(game->scenes());
    return game;
}

} // namespace pac::game
