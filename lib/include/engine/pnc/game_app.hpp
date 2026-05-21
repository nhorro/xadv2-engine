#pragma once

#include "engine/core/application.hpp"

#include <string>

namespace pac::pnc {

/// Composition root: register the built-in genre scenes and run the core harness
/// on the given manifest. This is what a game's `main` calls.
int run_game(const std::string& manifest_path, const pac::core::RunOptions& opts = {});

} // namespace pac::pnc
