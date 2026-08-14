#pragma once

#include "engine/core/application.hpp"

#include <string>

namespace example::notes {

int run_game(const std::string& manifest,
             const pac::core::RunOptions& options = {});
int run_game_from_resources(pac::core::ResourceSource& resources,
                            const std::string& manifest,
                            const pac::core::RunOptions& options = {});

} // namespace example::notes
