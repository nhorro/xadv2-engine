#pragma once

#include "engine/core/application.hpp"

#include <string>

namespace pac::pnc {

/// Composition root: register the built-in genre scenes and run the core harness
/// on the given manifest. This is what a game's `main` calls.
int run_game(const std::string& manifest_path, const pac::core::RunOptions& opts = {});

/// Resource-backed composition root for platforms whose packaged game data is
/// not an ordinary filesystem tree. Registers exactly the same built-in scenes
/// as `run_game`; only the resource source differs.
int run_game_from_resources(pac::core::ResourceSource& resources,
                            const std::string& manifest_logical_path,
                            const pac::core::RunOptions& opts = {});

/// The whole of a standard game's `main`: parse the command line (see
/// `core::parse_run_options`), then `run_game` the resulting manifest. A game
/// that adds C++ scenes of its own builds its own `SceneFactory` and calls
/// `core::run` instead — `parse_run_options` is still the way to read argv.
///
///     int main(int argc, char** argv) {
///         return pac::pnc::run_game_main(argc, argv, "data/game.yaml");
///     }
int run_game_main(int argc, char** argv, const std::string& default_manifest);

} // namespace pac::pnc
