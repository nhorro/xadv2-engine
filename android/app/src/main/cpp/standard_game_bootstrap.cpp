#include "android_game_bootstrap.hpp"

#include "engine/pnc/game_app.hpp"

namespace pac::android {

int run_game(pac::core::ResourceSource& resources, const std::string& manifest) {
    return pac::pnc::run_game_from_resources(resources, manifest);
}

} // namespace pac::android
