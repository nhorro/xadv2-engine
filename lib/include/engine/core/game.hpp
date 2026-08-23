#pragma once

#include "engine/core/application.hpp"
#include "engine/core/scene_factory.hpp"

#include <memory>
#include <string>

namespace pac::core {

/// Platform-independent compiled-game composition.
///
/// A game owns one derived instance for the duration of the engine run. Its
/// constructor registers scene types and hooks; any modules captured by those
/// hooks live as members of the derived object. Desktop, Android, and future
/// launchers all consume this same object and differ only in resource access.
class Game {
public:
    virtual ~Game() = default;

    SceneFactory& scenes() { return scenes_; }
    const SceneFactory& scenes() const { return scenes_; }

    ApplicationHooks& hooks() { return hooks_; }
    const ApplicationHooks& hooks() const { return hooks_; }

private:
    SceneFactory scenes_;
    ApplicationHooks hooks_;
};

/// Run one compiled game from normal filesystem/packed resources.
int run_game(const std::string& manifest_path,
             Game& game,
             const RunOptions& options = {});

/// Run the same compiled game from an explicitly supplied resource backend.
int run_game_from_resources(ResourceSource& resources,
                            const std::string& manifest_logical_path,
                            Game& game,
                            const RunOptions& options = {});

} // namespace pac::core

namespace pac::game {

/// Standard symbol implemented exactly once by a compiled game's `pac::game`
/// CMake target. Platform launchers never name a concrete game namespace.
std::unique_ptr<pac::core::Game> create();

} // namespace pac::game
