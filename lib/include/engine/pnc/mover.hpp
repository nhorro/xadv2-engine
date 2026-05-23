#pragma once

#include "engine/geom/geometry.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace pac::pnc {

struct RoomData;

/// The four cardinal facings a character can hold. Animation sequences are keyed
/// by direction (`<action>_<direction>`), so orientation renders at 90° steps
/// even though movement happens along arbitrary straight lines.
enum class Direction { Up, Right, Down, Left };

/// What the character is currently doing, used to pick the animation base.
enum class Action { Stand, Walk };

/// Snap a movement delta to the nearest of the four cardinal facings (screen
/// space: +y is down). Ties (|dx| == |dy|) and the zero vector resolve to the
/// horizontal axis — callers that must not change facing on a null move should
/// guard against a zero delta themselves. Pure logic, headless-testable.
Direction nearest_direction(geom::Point delta);

/// Round-trip with the script/data vocabulary (`"up"`/`"right"`/`"down"`/`"left"`).
/// An unrecognized string falls back to `Direction::Down`.
const char* to_string(Direction d);
Direction direction_from_string(const std::string& s);

/// Headless movement logic for a character: position, facing/action, and walking
/// an ordered path of waypoints, each leg a straight line gated by the room's
/// walkable area. Holds no graphics — the visual `Avatar` reads this state to
/// drive its sprite. Fully constructable and exercisable with no window.
class Mover {
public:
    void set_position(geom::Point p);
    geom::Point position() const { return pos_; }

    void face(Direction d) { facing_ = d; }
    Direction facing() const { return facing_; }
    Action action() const { return action_; }

    /// Walk straight to a single point.
    void move_to(geom::Point target);
    /// Walk an ordered list of waypoints, turning at each corner. An empty path
    /// stops the mover.
    void follow_path(std::vector<geom::Point> path);
    void stop();
    bool moving() const { return moving_; }

    /// Advance one frame: step toward the current waypoint, advancing to the next
    /// on arrival. A step that would leave the walkable area abandons the path.
    void update(float dt, const RoomData& room);

private:
    /// Aim at the next not-yet-reached waypoint, starting a walk. Returns false
    /// when the path is exhausted (caller should stop).
    bool start_leg();

    geom::Point pos_{0.0f, 0.0f};
    geom::Point target_{0.0f, 0.0f};
    std::vector<geom::Point> path_;
    std::size_t path_pos_ = 0;
    bool moving_ = false;
    float speed_ = 240.0f; // world px/s
    Direction facing_ = Direction::Down;
    Action action_ = Action::Stand;
};

} // namespace pac::pnc
