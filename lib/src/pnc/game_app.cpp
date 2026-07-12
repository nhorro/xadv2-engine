#include "engine/pnc/game_app.hpp"

#include "engine/core/scene_factory.hpp"
#include "engine/pnc/builtin_scenes.hpp"

namespace pac::pnc {

int run_game(const std::string& manifest_path, const pac::core::RunOptions& opts) {
    pac::core::SceneFactory factory;
    register_builtin_scenes(factory);
    return pac::core::run(manifest_path, factory, opts);
}

int run_game_main(int argc, char** argv, const std::string& default_manifest) {
    pac::core::RunOptions opts;
    const std::string manifest = pac::core::parse_run_options(argc, argv, opts, default_manifest);
    return run_game(manifest, opts);
}

} // namespace pac::pnc
