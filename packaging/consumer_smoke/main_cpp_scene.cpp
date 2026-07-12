// The other half of the packaging contract: a game that EXTENDS the engine in
// C++ — its own scene type, its own Lua functions — compiled against the
// installed engine.
//
// It exercises the two things a data-only game never touches, and which are
// therefore the two things an install is most likely to have forgotten to ship:
//
//   * engine/core/scripting_sol.hpp — the only public header that includes sol2,
//   * <sol/sol.hpp> itself, which must come from the package (pac::sol2).
//
// See examples/06_cpp_scene for the same pattern with an actual screen behind it.
#include "engine/core/application.hpp"
#include "engine/core/engine_context.hpp"
#include "engine/core/scene.hpp"
#include "engine/core/scene_factory.hpp"
#include "engine/core/scene_params.hpp"
#include "engine/core/scripting.hpp"
#include "engine/pnc/builtin_scenes.hpp"

#include <SFML/Graphics.hpp>
#include <sol/sol.hpp>

#include <memory>
#include <string>

namespace {

class EmptyScene : public pac::core::Scene {
public:
    explicit EmptyScene(pac::core::EngineContext& ctx) : ctx_(ctx) {}

    void draw(sf::RenderTarget& target) const override { target.clear(sf::Color(18, 20, 28)); }

private:
    pac::core::EngineContext& ctx_;
};

} // namespace

int main(int argc, char** argv) {
    pac::core::RunOptions opts;
    const std::string manifest = pac::core::parse_run_options(argc, argv, opts, "data/game.yaml");

    pac::core::SceneFactory factory;
    pac::pnc::register_builtin_scenes(factory);
    factory.register_type("ConsumerScene",
                          [](pac::core::EngineContext& ctx, const pac::core::SceneParams&) {
                              return std::make_unique<EmptyScene>(ctx);
                          });

    pac::core::ApplicationHooks hooks;
    hooks.configure = [](pac::core::EngineContext& ctx, const pac::core::Manifest&) {
        // The game reaching into the engine's Lua state to add its own API —
        // the reason sol2 is exported at all.
        sol::state& lua = ctx.scripting.lua();
        lua.set_function("consumer_hello", []() { return std::string("hello from the game"); });
    };

    return pac::core::run(manifest, factory, opts, hooks);
}
