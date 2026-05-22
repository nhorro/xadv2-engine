#pragma once

#include "engine/geom/geometry.hpp"
#include "engine/gfx/animated_sprite.hpp"

#include <string>

namespace sf {
class RenderTarget;
}

namespace pac::pnc {

struct RoomData;

/// The four cardinal facings an avatar can hold. Animation sequences are keyed
/// by direction (`<action>_<direction>`), so we only render orientation at 90°
/// steps even though movement happens along arbitrary straight lines.
enum class Direction { Up, Right, Down, Left };

/// Snap a movement delta to the nearest of the four cardinal facings (screen
/// space: +y is down). Ties (|dx| == |dy|) and the zero vector resolve to the
/// horizontal axis — callers that must not change facing on a null move should
/// guard against a zero delta themselves. Pure logic, headless-testable.
Direction nearest_direction(geom::Point delta);

/// Round-trip with the script/data vocabulary (`"up"`/`"right"`/`"down"`/`"left"`).
/// An unrecognized string falls back to `Direction::Down`.
const char* to_string(Direction d);
Direction direction_from_string(const std::string& s);

/// The in-room visual + spatial instance of a character. M3: a single
/// AnimatedSprite positioned by its walking pivot, with straight-line movement
/// gated by the room's walkable area (the stand-in behind the future pathfinder).
class Avatar {
public:
    explicit Avatar(gfx::AnimatedSprite sprite, float scale = 1.0f);

    void set_position(geom::Point p);
    geom::Point position() const { return pos_; }
    void face(const std::string& direction) { face(direction_from_string(direction)); }
    void face(Direction direction);
    std::string facing() const { return to_string(facing_); }

    void move_to(geom::Point target);
    void stop();
    bool moving() const { return moving_; }

    void update(float dt, const RoomData& room);
    void draw(sf::RenderTarget& target) const;

    float z() const { return pos_.y; } // depth key = walking-pivot y

private:
    enum class Action { Stand, Walk };

    void apply_position();
    /// Play the sequence for the current action + facing, falling back from
    /// `<action>_<direction>` to the bare `<action>` and finally to `stand`.
    void apply_animation();

    gfx::AnimatedSprite sprite_;
    geom::Point pos_{0.0f, 0.0f};
    geom::Point target_{0.0f, 0.0f};
    bool moving_ = false;
    float speed_ = 240.0f; // world px/s
    float scale_ = 1.0f;
    Direction facing_ = Direction::Down;
    Action action_ = Action::Stand;
};

} // namespace pac::pnc
