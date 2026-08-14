#include "android_game_bootstrap.hpp"

#include "game.hpp"

namespace pac::android {

int run_game(pac::core::ResourceSource& resources, const std::string& manifest) {
    return example::notes::run_game_from_resources(resources, manifest);
}

} // namespace pac::android
