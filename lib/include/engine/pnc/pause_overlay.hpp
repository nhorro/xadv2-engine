#pragma once

#include <string>
#include <vector>

namespace pac::core {
class Diagnostics;
class SceneManager;
class SceneParams;
}

namespace pac::pnc {

struct PauseOverlayAction {
    std::string scene;
    std::string label_key;
    int order = 30;
};

/// Parse sorted custom pause-overlay actions from flattened RoomScene params.
std::vector<PauseOverlayAction>
parse_pause_overlays(const pac::core::SceneParams& params, pac::core::Diagnostics& log);

/// Queue an overlay action through the scene stack.
void push_pause_overlay(const PauseOverlayAction& action, pac::core::SceneManager& scenes);

} // namespace pac::pnc
