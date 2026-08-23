#include "engine/core/game.hpp"

namespace pac::core {

int run_game(const std::string& manifest_path,
             Game& game,
             const RunOptions& options) {
    return run(manifest_path, game.scenes(), options, game.hooks());
}

int run_game_from_resources(ResourceSource& resources,
                            const std::string& manifest_logical_path,
                            Game& game,
                            const RunOptions& options) {
    return run_from_resources(
        resources, manifest_logical_path, game.scenes(), options, game.hooks());
}

} // namespace pac::core
