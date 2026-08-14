#pragma once

#include <string>

namespace pac::core {
class ResourceSource;
}

namespace pac::android {

// Implemented by the selected game composition. The Android shell owns asset
// access and lifecycle; the game still owns its SceneFactory and hooks.
int run_game(pac::core::ResourceSource& resources, const std::string& manifest);

} // namespace pac::android
