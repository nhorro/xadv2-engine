#include "game.hpp"

#include "engine/core/scene_factory.hpp"
#include "engine/pnc/builtin_scenes.hpp"
#include "field_notes.hpp"

namespace example::notes {
namespace {

struct Composition {
    pac::core::SceneFactory factory;
    FieldNotesModule notes{"notes"};
    pac::core::ApplicationHooks hooks;

    Composition() {
        pac::pnc::register_builtin_scenes(factory);
        notes.register_scenes(factory);
        hooks.configure = [this](pac::core::EngineContext& ctx,
                                 const pac::core::Manifest& manifest) {
            notes.configure(ctx, manifest);
        };
    }
};

} // namespace

int run_game(const std::string& manifest, const pac::core::RunOptions& options) {
    Composition game;
    return pac::core::run(manifest, game.factory, options, game.hooks);
}

int run_game_from_resources(pac::core::ResourceSource& resources,
                            const std::string& manifest,
                            const pac::core::RunOptions& options) {
    Composition game;
    return pac::core::run_from_resources(resources, manifest, game.factory, options, game.hooks);
}

} // namespace example::notes
