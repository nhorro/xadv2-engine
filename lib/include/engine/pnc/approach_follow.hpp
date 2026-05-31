#pragma once

#include "engine/geom/geometry.hpp"

namespace pac::pnc {

/// What a moving-target approach should do on a given frame (#158): keep walking
/// the current path, re-path toward the target's new position, or fire the verb.
enum class ChaseAction { Wait, Repath, Fire };

/// Tunables for a moving-target chase. `interaction_range` is how close (world px)
/// the player must get to the live target before the verb fires. `repath_threshold`
/// is how far the target may drift from the path's destination before we re-route.
/// `timeout` (seconds, <= 0 disables) bounds the chase so a fleeing target can't
/// stall the player forever — once it elapses the verb fires from wherever we are.
struct ChaseParams {
    float interaction_range = 40.0f;
    float repath_threshold = 16.0f;
    float timeout = 6.0f;
};

/// Decision for `evaluate_chase`. `repath_to` is meaningful only when
/// `action == Repath` (the live target position to route toward).
struct ChaseDecision {
    ChaseAction action = ChaseAction::Wait;
    geom::Point repath_to{0.0f, 0.0f};
};

/// Decide what a walk-then-act approach to a *moving* bound target should do this
/// frame. Pure logic (no avatar, no pathfinding, no window) so it is headless-
/// testable; the caller wires the result to the avatar + `find_path` + dispatch.
///
///   - `player`    : the avatar's current position.
///   - `target`    : the bound NPC/object's live position this frame.
///   - `last_dest` : the destination the current path was routed toward.
///   - `moving`    : whether the avatar is still walking its path.
///   - `elapsed`   : seconds since the chase began (for the give-up timeout).
///
/// Returns `Fire` when in range, when the timeout elapses, or when the avatar has
/// stopped as close as the walkable area allows and the target is not fleeing;
/// `Repath` when the target has drifted past `repath_threshold` (or the avatar
/// stopped short while the target moved); otherwise `Wait`.
ChaseDecision evaluate_chase(geom::Point player,
                             geom::Point target,
                             geom::Point last_dest,
                             bool moving,
                             float elapsed,
                             const ChaseParams& params);

} // namespace pac::pnc
