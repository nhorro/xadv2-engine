#include "engine/pnc/game_app.hpp"

#include "engine/core/scene_factory.hpp"
#include "engine/pnc/builtin_scenes.hpp"

namespace pac::pnc {

int run_game(const std::string& manifest_path, const pac::core::RunOptions& opts) {
    pac::core::SceneFactory factory;
    register_builtin_scenes(factory);
    return pac::core::run(manifest_path, factory, opts);
}

} // namespace pac::pnc
