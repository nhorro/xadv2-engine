#include "engine/pnc/pause_overlay.hpp"

#include "engine/core/diagnostics.hpp"
#include "engine/core/scene_manager.hpp"
#include "engine/core/scene_params.hpp"

#include <algorithm>
#include <exception>

namespace pac::pnc {

std::vector<PauseOverlayAction>
parse_pause_overlays(const pac::core::SceneParams& params, pac::core::Diagnostics& log) {
    std::vector<PauseOverlayAction> overlays;
    for (const std::string& id : params.children("pause_menu.overlays")) {
        const std::string prefix = "pause_menu.overlays." + id + ".";
        PauseOverlayAction overlay;
        overlay.scene = params.get_or(prefix + "scene", "");
        overlay.label_key = params.get_or(prefix + "label_key", id);
        try {
            overlay.order = std::stoi(params.get_or(prefix + "order", "30"));
        } catch (const std::exception&) {
            log.warn("RoomScene: invalid pause overlay order for '" + id + "'; using 30");
        }
        if (overlay.scene.empty()) {
            log.warn("RoomScene: pause overlay '" + id + "' has no scene; ignoring it");
            continue;
        }
        if (overlay.order < 30 || overlay.order > 89) {
            log.warn("RoomScene: pause overlay '" + id + "' order must be 30..89; using 30");
            overlay.order = 30;
        }
        overlays.push_back(std::move(overlay));
    }
    std::stable_sort(overlays.begin(), overlays.end(), [](const auto& a, const auto& b) {
        return a.order < b.order;
    });
    return overlays;
}

void push_pause_overlay(const PauseOverlayAction& action, pac::core::SceneManager& scenes) {
    scenes.push_scene(action.scene);
}

} // namespace pac::pnc
