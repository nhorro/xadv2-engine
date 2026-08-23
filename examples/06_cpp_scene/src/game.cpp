#include "engine/core/game.hpp"

#include "engine/pnc/builtin_scenes.hpp"
#include "field_notes.hpp"

namespace {

class FieldNotesGame final : public pac::core::Game {
public:
    FieldNotesGame() {
        pac::pnc::register_builtin_scenes(scenes());
        notes_.register_scenes(scenes());
        hooks().configure = [this](pac::core::EngineContext& ctx,
                                   const pac::core::Manifest& manifest) {
            notes_.configure(ctx, manifest);
        };
    }

private:
    example::notes::FieldNotesModule notes_{"notes"};
};

} // namespace

namespace pac::game {

std::unique_ptr<pac::core::Game> create() {
    return std::make_unique<FieldNotesGame>();
}

} // namespace pac::game
