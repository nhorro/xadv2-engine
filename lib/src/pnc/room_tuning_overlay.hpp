#pragma once

#include "engine/pnc/room.hpp"

#include <SFML/Graphics/Rect.hpp>
#include <SFML/System/Vector2.hpp>
#include <SFML/Window/Event.hpp>

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace sf {
class Font;
class RenderTarget;
} // namespace sf

namespace pac::pnc {

/// Development-only, in-engine lighting/post-process tuning panel. The panel
/// edits the room's live render state while retaining the authored snapshot for
/// reset/A-B comparison.
class RoomTuningOverlay {
public:
    enum class Tab { AMBIENT, LIGHTS, GRADING };

    void open(RoomRenderState& live,
              const RoomRenderState& authored,
              sf::FloatRect panel_region,
              const sf::Font* font);
    void close();
    [[nodiscard]] bool active() const { return active_; }

    /// Consume an event in virtual display coordinates. Returns true whenever
    /// the overlay is active, including events that miss a control, so gameplay
    /// input can never leak through to the SCUMM command system.
    bool handle_event(const sf::Event& event);
    void draw(sf::RenderTarget& target);

    [[nodiscard]] const RoomLighting* effective_lighting();
    [[nodiscard]] const RoomPostProcess* effective_post_process();
    [[nodiscard]] const ProjectedShadow* effective_projected_shadow();

    void reset();
    [[nodiscard]] std::string yaml() const;

    // Small state accessors used by headless regression tests.
    [[nodiscard]] RoomLighting& working_lighting() { return working_lighting_; }
    [[nodiscard]] std::optional<RoomPostProcess>& working_post_process() {
        return working_post_process_;
    }
    [[nodiscard]] bool compare_original() const { return compare_original_; }

private:
    enum class LightPage { CORE, SHAPE, MODULATION };
    enum class ControlType { BUTTON, SLIDER };

    struct Control {
        ControlType type = ControlType::BUTTON;
        sf::FloatRect rect;
        std::string label;
        float value = 0.0f;
        float minimum = 0.0f;
        float maximum = 1.0f;
        float step = 0.0f;
        std::function<void(float)> action;
    };

    void rebuild_controls();
    void add_button(sf::FloatRect rect, std::string label, std::function<void()> action);
    void add_slider(sf::FloatRect rect,
                    std::string label,
                    float value,
                    float minimum,
                    float maximum,
                    float step,
                    std::function<void(float)> action);
    void activate(Control& control, float mouse_x);
    [[nodiscard]] RoomLight* selected_light();
    [[nodiscard]] gfx::ShaderEffect* selected_effect();
    [[nodiscard]] gfx::ShaderParam* selected_param();
    void clamp_selection();
    void copy_yaml();

    bool active_ = false;
    bool compare_original_ = false;
    bool had_original_lighting_ = false;
    RoomLighting original_lighting_;
    RoomLighting working_lighting_;
    std::optional<RoomPostProcess> original_post_process_;
    std::optional<RoomPostProcess> working_post_process_;
    std::optional<ProjectedShadow> original_projected_shadow_;
    std::optional<ProjectedShadow> projected_shadow_;
    RoomRenderState* live_ = nullptr;

    sf::FloatRect region_;
    const sf::Font* font_ = nullptr;
    sf::Vector2f pointer_{-1000.0f, -1000.0f};
    std::vector<Control> controls_;
    std::optional<std::size_t> dragged_control_;
    Tab tab_ = Tab::AMBIENT;
    LightPage light_page_ = LightPage::CORE;
    std::size_t selected_light_ = 0;
    std::size_t selected_effect_ = 0;
    std::size_t selected_param_ = 0;
    std::size_t selected_component_ = 0;
    std::string status_;
};

} // namespace pac::pnc
