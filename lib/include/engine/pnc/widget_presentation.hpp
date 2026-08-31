#pragma once

#include <SFML/Graphics/Rect.hpp>
#include <SFML/System/Vector2.hpp>

#include <functional>
#include <memory>

namespace sf {
class RenderTarget;
class RenderTexture;
}

namespace pac::pnc {

enum class WidgetVisibility { HIDDEN, SHOWING, VISIBLE, HIDING };
enum class WidgetAnchor {
    TOP_LEFT,
    TOP_CENTER,
    TOP_RIGHT,
    CENTER_LEFT,
    CENTER,
    CENTER_RIGHT,
    BOTTOM_LEFT,
    BOTTOM_CENTER,
    BOTTOM_RIGHT,
};

struct WidgetTransition {
    float fade_duration = 0.20f;
    bool capture_while_hiding = true;
};

/// Declarative placement inside a screen-space container. `position` is a
/// normalized point (0..1); `anchor` selects which point of the widget is attached
/// there, and `offset` applies runtime-pixel fine tuning.
struct WidgetPlacement {
    sf::Vector2f position{0.5f, 0.5f};
    WidgetAnchor anchor = WidgetAnchor::CENTER;
    sf::Vector2f offset{};
};

[[nodiscard]] sf::Vector2f place_widget(sf::FloatRect container,
                                        sf::Vector2f widget_size,
                                        const WidgetPlacement& placement);

/// Standard retained presentation properties shared by all room UI widgets.
/// Geometry and animation are screen-space concerns and never feed room/camera
/// calculations. Translation is live now even though fade is the first animated
/// property, leaving slide transitions as a policy addition rather than a redesign.
class WidgetPresentation {
public:
    explicit WidgetPresentation(bool initially_visible = true,
                                WidgetTransition transition = {});

    void set_bounds(sf::FloatRect bounds) { bounds_ = bounds; }
    void set_position(sf::Vector2f position);
    void set_size(sf::Vector2f size);
    void set_translation(sf::Vector2f translation) { translation_ = translation; }
    void set_opacity(float opacity);
    void set_input_enabled(bool enabled) { input_enabled_ = enabled; }

    void show();
    void hide();
    void update(float dt);

    [[nodiscard]] sf::FloatRect bounds() const;
    [[nodiscard]] sf::Vector2f position() const { return {bounds_.left, bounds_.top}; }
    [[nodiscard]] sf::Vector2f size() const { return {bounds_.width, bounds_.height}; }
    [[nodiscard]] sf::Vector2f translation() const { return translation_; }
    [[nodiscard]] float opacity() const;
    [[nodiscard]] WidgetVisibility visibility() const { return visibility_; }
    [[nodiscard]] bool rendered() const { return visibility_ != WidgetVisibility::HIDDEN; }
    [[nodiscard]] bool captures_input() const;

private:
    sf::FloatRect bounds_;
    sf::Vector2f translation_;
    WidgetTransition transition_;
    WidgetVisibility visibility_ = WidgetVisibility::VISIBLE;
    float progress_ = 1.0f;
    float base_opacity_ = 1.0f;
    bool input_enabled_ = true;
};

/// Renders an absolute-coordinate widget into a transparent local surface and
/// composites it with the presentation's opacity/translation. Painters do not
/// need to know which property is currently animated.
class WidgetSurface {
public:
    WidgetSurface();
    ~WidgetSurface();
    WidgetSurface(WidgetSurface&&) noexcept;
    WidgetSurface& operator=(WidgetSurface&&) noexcept;
    WidgetSurface(const WidgetSurface&) = delete;
    WidgetSurface& operator=(const WidgetSurface&) = delete;

    void draw(sf::RenderTarget& target,
              const WidgetPresentation& presentation,
              sf::FloatRect content_bounds,
              const std::function<void(sf::RenderTarget&)>& painter) const;

private:
    mutable std::unique_ptr<sf::RenderTexture> texture_;
};

} // namespace pac::pnc
