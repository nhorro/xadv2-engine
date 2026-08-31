#include "engine/pnc/room_command_processor.hpp"

#include "engine/pnc/approach_follow.hpp"

namespace pac::pnc {

namespace {
constexpr float kApproachReached = 8.0f;
}

RoomCommandProcessor::RoomCommandProcessor(RoomCommandProcessorHost& host) : host_(host) {}

CommandSubmission RoomCommandProcessor::submit(Command command) {
    if (!host_.command_submission_enabled() || !valid(command)) {
        reject();
        return CommandSubmission::REJECTED;
    }

    const ObjectRef* target_ref = room_target(command);
    const RoomCommandTarget target =
        target_ref ? host_.resolve_room_command_target(target_ref->id) : RoomCommandTarget{};
    if (target_ref && !target.found) {
        reject();
        return CommandSubmission::REJECTED;
    }

    if (target_ref && target.approach && host_.command_player_available()) {
        const geom::Point before = host_.command_player_position();
        const geom::Point destination =
            host_.route_command_player_to(*target.approach, target_ref->id);
        if (target.requires_approach && geom::distance(before, destination) > kApproachReached) {
            const std::string hotspot_id = target_ref->id;
            pending_approach_ = PendingApproach{std::move(command), hotspot_id};
            pending_moving_approach_.reset();
            host_.block_for_command();
            return CommandSubmission::DEFERRED;
        }
    }

    if (target_ref && target.requires_approach && target.moving && !target.approach &&
        target.live_position && host_.command_player_available()) {
        const ChaseParams params;
        if (geom::distance(host_.command_player_position(), *target.live_position) >
            params.interaction_range) {
            const std::string hotspot_id = target_ref->id;
            const geom::Point destination = host_.reroute_command_player_to(*target.live_position);
            pending_approach_.reset();
            pending_moving_approach_ =
                PendingMovingApproach{std::move(command), hotspot_id, destination, 0.0f};
            host_.block_for_command();
            return CommandSubmission::DEFERRED;
        }
    }

    dispatch_and_feedback(command);
    return CommandSubmission::DISPATCHED;
}

void RoomCommandProcessor::update(float dt) {
    if (pending_approach_ && host_.command_player_available() && !host_.command_player_moving()) {
        const Command command = pending_approach_->command;
        pending_approach_.reset();
        host_.unblock_after_command();
        if (valid(command)) {
            dispatch_and_feedback(command);
        } else {
            reject();
        }
    }

    if (!pending_moving_approach_ || !host_.command_player_available()) {
        return;
    }

    PendingMovingApproach& pending = *pending_moving_approach_;
    pending.elapsed += dt;
    const RoomCommandTarget target = host_.resolve_room_command_target(pending.hotspot_id);
    if (!target.found || !target.live_position || !valid(pending.command)) {
        pending_moving_approach_.reset();
        host_.unblock_after_command();
        reject();
        return;
    }

    const ChaseParams params;
    const ChaseDecision decision = evaluate_chase(host_.command_player_position(),
                                                  *target.live_position,
                                                  pending.last_destination,
                                                  host_.command_player_moving(),
                                                  pending.elapsed,
                                                  params);
    if (decision.action == ChaseAction::Fire) {
        const Command command = pending.command;
        pending_moving_approach_.reset();
        host_.unblock_after_command();
        dispatch_and_feedback(command);
    } else if (decision.action == ChaseAction::Repath) {
        pending.last_destination = host_.reroute_command_player_to(decision.repath_to);
    }
}

void RoomCommandProcessor::reset() {
    pending_approach_.reset();
    pending_moving_approach_.reset();
}

bool RoomCommandProcessor::has_deferred_command() const {
    return pending_approach_.has_value() || pending_moving_approach_.has_value();
}

void RoomCommandProcessor::cancel_deferred_command() {
    reset();
}

bool RoomCommandProcessor::valid(const Command& command) const {
    if (!command.param1.valid() ||
        !kind_matches(command.param1.kind, verb_param1_class(command.verb))) {
        return false;
    }
    const CommandOperandValidation first =
        host_.validate_command_operand(command.param1, command.verb);
    if (!first.found || !first.affords_verb) {
        return false;
    }

    const bool give = command.verb == Verb::GIVE;
    const bool combinable_use = command.verb == Verb::USE &&
                                command.param1.kind == ObjectKind::INVENTORY_OBJECT &&
                                first.combinable;
    if (give || combinable_use) {
        if (!command.param2 || !command.param2->valid()) {
            return false;
        }
        if (give && command.param2->kind != ObjectKind::ROOM_OBJECT) {
            return false;
        }
    } else if (command.param2) {
        return false;
    }

    if (command.param2) {
        const CommandOperandValidation second =
            host_.validate_command_operand(*command.param2, command.verb);
        if (!second.found || !second.affords_verb) {
            return false;
        }
    }
    return true;
}

const ObjectRef* RoomCommandProcessor::room_target(const Command& command) const {
    if (command.param2 && command.param2->kind == ObjectKind::ROOM_OBJECT) {
        return &*command.param2;
    }
    if (command.param1.kind == ObjectKind::ROOM_OBJECT) {
        return &command.param1;
    }
    return nullptr;
}

VerbResult RoomCommandProcessor::dispatch(const Command& command) {
    const std::string verb(verb_id(command.verb));
    const ObjectRef& first = command.param1;
    if (command.param2) {
        const ObjectRef& second = *command.param2;
        if (first.kind == ObjectKind::INVENTORY_OBJECT) {
            if (VerbResult result = host_.call_inventory_command(first.id, verb, second.id);
                result.handled) {
                return result;
            }
        }
        if (second.kind == ObjectKind::ROOM_OBJECT) {
            if (VerbResult result = host_.call_hotspot_command(second.id, verb, first.id);
                result.handled) {
                return result;
            }
        }
        return host_.call_game_command(verb, first.id, second.id);
    }

    if (first.kind == ObjectKind::INVENTORY_OBJECT) {
        if (VerbResult result = host_.call_inventory_command(first.id, verb, std::nullopt);
            result.handled) {
            return result;
        }
    } else if (first.kind == ObjectKind::ROOM_OBJECT) {
        if (VerbResult result = host_.call_hotspot_command(first.id, verb, std::nullopt);
            result.handled) {
            return result;
        }
    }
    return host_.call_game_command(verb, first.id, std::nullopt);
}

void RoomCommandProcessor::dispatch_and_feedback(const Command& command) {
    host_.begin_command_dispatch();
    const bool player_was_moving =
        host_.command_player_available() && host_.command_player_moving();
    const std::string movement_facing =
        host_.command_player_available() ? host_.command_player_facing() : std::string();
    host_.face_command_target(command);
    const VerbResult result = dispatch(command);

    if (!host_.command_view_active()) {
        host_.finish_command_execution();
        return;
    }
    if (result.in_flight) {
        host_.arm_command_handler_task(*result.in_flight);
        host_.block_for_command();
        return;
    }
    if (host_.command_handler_task_armed()) {
        host_.block_for_command();
        return;
    }

    if (player_was_moving && host_.command_player_available() && host_.command_player_moving()) {
        host_.restore_command_player_facing(movement_facing);
    }
    if (result.caption) {
        host_.present_command_caption(*result.caption);
    } else if (!result.handled && !host_.command_spoke_during_dispatch()) {
        host_.present_unhandled_command();
    }
    host_.finish_command_execution();
}

void RoomCommandProcessor::reject() {
    host_.finish_command_execution();
}

} // namespace pac::pnc
