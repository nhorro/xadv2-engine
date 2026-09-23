#pragma once

#include "engine/core/manifest.hpp"
#include "engine/core/scripting.hpp"

#include <SFML/Graphics/Color.hpp>
#include <SFML/System/Vector2.hpp>

#include <cstdint>
#include <optional>
#include <string>

namespace sf {
class Event;
class Font;
class RenderTarget;
class Texture;
} // namespace sf

namespace pac::core {

class Diagnostics;
class Display;
class ResourceCache;

enum class IndicatorDirection { Down, Up, Left, Right };

struct InformationIndicator {
    sf::Vector2f position{0.0f, 0.0f};
    IndicatorDirection direction = IndicatorDirection::Down;
};

struct InformationPage {
    std::string text;
    std::string image;
    std::string dismiss_text;
    std::optional<InformationIndicator> indicator;
};

/// Application-level, scene-independent player guidance. A modal information
/// page freezes the active scene and consumes its input until dismissed; the
/// target indicator can also be shown independently without blocking gameplay.
class InformationOverlay {
public:
    InformationOverlay(Display& display,
                       ResourceCache& resources,
                       Scripting& scripting,
                       Diagnostics& log,
                       InformationOverlayConfig config);

    /// Show a page and return the scoped event emitted when it is dismissed.
    std::string show(InformationPage page, ScopeId scope);
    void dismiss();

    void show_indicator(InformationIndicator indicator);
    void hide_indicator();

    [[nodiscard]] bool modal_active() const { return page_.has_value(); }
    [[nodiscard]] bool indicator_active() const { return indicator_.has_value(); }
    [[nodiscard]] const InformationPage* page() const { return page_ ? &*page_ : nullptr; }

    /// Returns true while the modal owns the event. Release/click activation
    /// dismisses it; every other event is swallowed to protect the scene below.
    bool handle_event(const sf::Event& event);
    void update(float dt);
    void draw(sf::RenderTarget& target) const;

private:
    void draw_indicator(sf::RenderTarget& target) const;
    void draw_page(sf::RenderTarget& target) const;

    Display& display_;
    ResourceCache& resources_;
    Scripting& scripting_;
    Diagnostics& log_;
    InformationOverlayConfig config_;
    const sf::Font* font_ = nullptr;
    mutable const sf::Texture* indicator_texture_ = nullptr;
    mutable const sf::Texture* page_texture_ = nullptr;
    mutable bool indicator_texture_attempted_ = false;
    mutable bool page_texture_attempted_ = false;
    std::optional<InformationPage> page_;
    std::optional<InformationIndicator> indicator_;
    std::optional<InformationIndicator> saved_indicator_;
    ScopeId waiting_scope_ = 0;
    std::string waiting_event_;
    std::uint64_t event_serial_ = 0;
    float elapsed_ = 0.0f;
};

} // namespace pac::core
