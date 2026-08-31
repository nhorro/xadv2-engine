#pragma once

#include "engine/pnc/routed_input.hpp"

#include <SFML/Graphics/Rect.hpp>

#include <vector>

namespace sf {
class RenderTarget;
}

namespace pac::pnc {

enum class InputResult { PASS, CONSUMED };

class RoomInputLayer {
public:
    virtual ~RoomInputLayer() = default;
    [[nodiscard]] virtual InputResult handle(const RoutedInput& input) = 0;
};

/// Ordered first-consumer-wins router. Layers are non-owning and must outlive
/// their registration; scene composition owns widgets and adapters.
class RoomInputRouter {
public:
    void add(RoomInputLayer& layer);
    void clear();
    [[nodiscard]] InputResult route(const RoutedInput& input);

private:
    std::vector<RoomInputLayer*> layers_;
};

/// Minimal composable widget contract. Presentation transitions will extend the
/// visibility state later without changing routing or room command behavior.
class UiWidget : public RoomInputLayer {
public:
    [[nodiscard]] virtual sf::FloatRect input_bounds() const = 0;
    [[nodiscard]] virtual bool captures(sf::Vector2f point) const = 0;
    virtual void update(float dt) = 0;
    virtual void draw(sf::RenderTarget& target) const = 0;
};

} // namespace pac::pnc
