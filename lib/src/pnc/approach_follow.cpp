#include "engine/pnc/approach_follow.hpp"

namespace pac::pnc {

ChaseDecision evaluate_chase(geom::Point player,
                             geom::Point target,
                             geom::Point last_dest,
                             bool moving,
                             float elapsed,
                             const ChaseParams& params) {
    // In range: act now.
    if (geom::distance(player, target) <= params.interaction_range) {
        return {ChaseAction::Fire, target};
    }
    // Give up after the timeout so a target fleeing faster than the player can't
    // stall the chase forever — fire from the current distance.
    if (params.timeout > 0.0f && elapsed >= params.timeout) {
        return {ChaseAction::Fire, target};
    }

    const float drift = geom::distance(target, last_dest);
    if (!moving) {
        // The avatar reached the end of its path but isn't in range. If the target
        // has since moved, chase its new position; otherwise we're as close as the
        // walkable area allows and the target is holding still, so act from here.
        if (drift > params.repath_threshold) {
            return {ChaseAction::Repath, target};
        }
        return {ChaseAction::Fire, target};
    }
    // Still walking: re-route only once the target has drifted enough from where we
    // were heading, so we retarget a moving NPC without re-pathing every frame.
    if (drift > params.repath_threshold) {
        return {ChaseAction::Repath, target};
    }
    return {ChaseAction::Wait, target};
}

} // namespace pac::pnc
