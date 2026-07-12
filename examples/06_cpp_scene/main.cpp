// 06_cpp_scene — a game that adds a scene type of its own.
//
// The other examples' main() is one line (`run_game_main`), because they need no
// C++ at all. This one builds the SceneFactory by hand so it can add its own
// type on top of the built-in ones, and passes an ApplicationHooks::configure so
// the module can install its Lua API once the engine is up.
#include "field_notes.hpp"

#include "engine/core/application.hpp"
#include "engine/core/scene_factory.hpp"
#include "engine/pnc/builtin_scenes.hpp"

#include <string>

int main(int argc, char** argv) {
    pac::core::RunOptions opts;
    const std::string manifest =
        pac::core::parse_run_options(argc, argv, opts, "examples/06_cpp_scene/data/game.yaml");

    pac::core::SceneFactory factory;
    pac::pnc::register_builtin_scenes(factory);   // RoomScene, Cutscene, CloseUp, ...

    // ... and one more, ours. The manifest scene with `type: FieldNotes` builds it.
    example::notes::FieldNotesModule notes("notes");
    notes.register_scenes(factory);

    pac::core::ApplicationHooks hooks;
    hooks.configure = [&notes](pac::core::EngineContext& ctx,
                               const pac::core::Manifest& manifest_data) {
        notes.configure(ctx, manifest_data);
    };

    return pac::core::run(manifest, factory, opts, hooks);
}
