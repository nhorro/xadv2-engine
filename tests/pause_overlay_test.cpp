#include "engine/pnc/pause_overlay.hpp"

#include "engine/core/diagnostics.hpp"
#include "engine/core/scene.hpp"
#include "engine/core/scene_manager.hpp"
#include "engine/core/scene_params.hpp"

#include <doctest/doctest.h>

#include <memory>
#include <string>

namespace {

class TestScene : public pac::core::Scene {
public:
    void draw(sf::RenderTarget&) const override {}
};

} // namespace

TEST_CASE("pause overlays parse in configured order and push through the scene stack") {
    pac::core::SceneParams params;
    params.set("pause_menu.overlays.codex.scene", "codex");
    params.set("pause_menu.overlays.codex.label_key", "codex");
    params.set("pause_menu.overlays.codex.order", "40");
    params.set("pause_menu.overlays.notebook.scene", "notebook");
    params.set("pause_menu.overlays.notebook.label_key", "notebook");
    params.set("pause_menu.overlays.notebook.order", "30");

    pac::core::Diagnostics log(pac::core::LogLevel::ERROR);
    const auto overlays = pac::pnc::parse_pause_overlays(params, log);
    REQUIRE(overlays.size() == 2);
    CHECK(overlays[0].scene == "notebook");
    CHECK(overlays[0].order == 30);
    CHECK(overlays[1].scene == "codex");

    pac::core::SceneManager scenes;
    scenes.set_builder([](const std::string&) { return std::make_unique<TestScene>(); });
    scenes.goto_scene("room");
    scenes.apply_pending();
    pac::pnc::push_pause_overlay(overlays[0], scenes);
    scenes.apply_pending();
    CHECK(scenes.size() == 2);
}
