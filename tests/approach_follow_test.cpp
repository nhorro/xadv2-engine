#include "engine/pnc/approach_follow.hpp"

#include <doctest/doctest.h>

using namespace pac::pnc;

namespace {
// Defaults: interaction_range 40, repath_threshold 16, timeout 6s.
ChaseParams params() {
    return ChaseParams{};
}
} // namespace

TEST_CASE("evaluate_chase fires once the player is within interaction range") {
    const ChaseDecision d =
        evaluate_chase({0.0f, 0.0f}, {30.0f, 0.0f}, {30.0f, 0.0f}, true, 1.0f, params());
    CHECK(d.action == ChaseAction::Fire);
}

TEST_CASE("evaluate_chase waits while walking toward a target that has not drifted") {
    // Out of range, still moving, target sits where the path was routed -> keep walking.
    const ChaseDecision d =
        evaluate_chase({0.0f, 0.0f}, {100.0f, 0.0f}, {100.0f, 0.0f}, true, 1.0f, params());
    CHECK(d.action == ChaseAction::Wait);
}

TEST_CASE("evaluate_chase re-paths to the live position when the target drifts") {
    // Target moved 30px from the path destination (> 16 threshold) -> re-route to it.
    const ChaseDecision d =
        evaluate_chase({0.0f, 0.0f}, {130.0f, 0.0f}, {100.0f, 0.0f}, true, 1.0f, params());
    REQUIRE(d.action == ChaseAction::Repath);
    CHECK(d.repath_to.x == doctest::Approx(130.0f));
    CHECK(d.repath_to.y == doctest::Approx(0.0f));
}

TEST_CASE("evaluate_chase acts from the closest point when stopped and the target holds still") {
    // Avatar stopped (path exhausted), out of range, target not drifting -> fire here
    // rather than shuffle in place (can't get any closer).
    const ChaseDecision d =
        evaluate_chase({0.0f, 0.0f}, {100.0f, 0.0f}, {100.0f, 0.0f}, false, 1.0f, params());
    CHECK(d.action == ChaseAction::Fire);
}

TEST_CASE("evaluate_chase re-paths when stopped short and the target has since moved") {
    const ChaseDecision d =
        evaluate_chase({0.0f, 0.0f}, {130.0f, 0.0f}, {100.0f, 0.0f}, false, 1.0f, params());
    REQUIRE(d.action == ChaseAction::Repath);
    CHECK(d.repath_to.x == doctest::Approx(130.0f));
}

TEST_CASE("evaluate_chase gives up and fires from distance once the timeout elapses") {
    // Far, still walking, target far from the path dest: normally Repath, but the
    // timeout wins so a faster-fleeing target can't stall the player forever.
    const ChaseDecision d =
        evaluate_chase({0.0f, 0.0f}, {500.0f, 0.0f}, {490.0f, 0.0f}, true, 6.0f, params());
    CHECK(d.action == ChaseAction::Fire);
}

TEST_CASE("evaluate_chase with timeout disabled never gives up on time alone") {
    ChaseParams p = params();
    p.timeout = 0.0f; // disabled
    const ChaseDecision d =
        evaluate_chase({0.0f, 0.0f}, {100.0f, 0.0f}, {100.0f, 0.0f}, true, 999.0f, p);
    CHECK(d.action == ChaseAction::Wait);
}
