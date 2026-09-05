#pragma once

#include "engine/core/scripting.hpp"
#include "engine/pnc/command.hpp"
#include "engine/pnc/verb_result.hpp"

#include <SFML/System/Vector2.hpp>

#include <optional>
#include <string>

namespace pac::pnc {

enum class CommandSubmission { REJECTED, DISPATCHED, DEFERRED };

/// Source-agnostic boundary for completed room commands. A SCUMM composer,
/// automated test, debug adapter, or future socket adapter can all submit the
/// same domain value without synthesizing pointer input.
class RoomCommandSink {
public:
    virtual ~RoomCommandSink() = default;
    [[nodiscard]] virtual CommandSubmission submit(Command command) = 0;
};

struct CommandOperandValidation {
    bool found = false;
    bool affords_verb = false;
    bool combinable = false;
};

/// Snapshot of the room target needed for approach policy. It contains values,
/// never pointers into RoomRuntime, so a room change cannot leave stale aliases.
struct RoomCommandTarget {
    bool found = false;
    std::optional<sf::Vector2f> approach;
    bool requires_approach = false;
    bool moving = false;
    std::optional<sf::Vector2f> live_position;
};

/// Narrow live-session seam used by RoomCommandProcessor. Presentation and
/// platform input stay on the host side; the processor owns command policy.
class RoomCommandProcessorHost {
public:
    virtual ~RoomCommandProcessorHost() = default;

    [[nodiscard]] virtual bool command_submission_enabled() const = 0;
    [[nodiscard]] virtual CommandOperandValidation validate_command_operand(const ObjectRef& object,
                                                                            Verb verb) const = 0;
    [[nodiscard]] virtual RoomCommandTarget
    resolve_room_command_target(const std::string& hotspot_id) const = 0;

    [[nodiscard]] virtual bool command_player_available() const = 0;
    [[nodiscard]] virtual sf::Vector2f command_player_position() const = 0;
    [[nodiscard]] virtual bool command_player_moving() const = 0;
    [[nodiscard]] virtual std::string command_player_facing() const = 0;
    virtual sf::Vector2f route_command_player_to(sf::Vector2f target,
                                                 const std::string& hotspot_id) = 0;
    virtual sf::Vector2f reroute_command_player_to(sf::Vector2f target) = 0;
    virtual void face_command_target(const Command& command) = 0;
    virtual void restore_command_player_facing(const std::string& facing) = 0;

    virtual VerbResult call_inventory_command(const std::string& item_id,
                                              const std::string& verb,
                                              std::optional<std::string> operand) = 0;
    virtual VerbResult call_hotspot_command(const std::string& hotspot_id,
                                            const std::string& verb,
                                            std::optional<std::string> operand) = 0;
    virtual VerbResult call_game_command(const std::string& verb,
                                         const std::string& first,
                                         std::optional<std::string> second) = 0;

    /// Called once for every valid, accepted player command, before any required
    /// approach walk. Default no-op keeps headless hosts transport-agnostic.
    virtual void record_command_submission(const Command&) {}
    virtual void begin_command_dispatch() = 0;
    [[nodiscard]] virtual bool command_view_active() const = 0;
    [[nodiscard]] virtual bool command_handler_task_armed() const = 0;
    virtual void arm_command_handler_task(pac::core::TaskId task) = 0;
    virtual void block_for_command() = 0;
    virtual void unblock_after_command() = 0;
    [[nodiscard]] virtual bool command_spoke_during_dispatch() const = 0;
    virtual void present_command_caption(const std::string& caption) = 0;
    virtual void present_unhandled_command() = 0;
    virtual void finish_command_execution() = 0;
};

/// Executes completed domain commands independently of their input source.
/// Owns validation, approach/deferred execution, handler precedence, and
/// feedback selection; it never sees UI classes, rendering, or input events.
class RoomCommandProcessor final : public RoomCommandSink {
public:
    explicit RoomCommandProcessor(RoomCommandProcessorHost& host);

    [[nodiscard]] CommandSubmission submit(Command command) override;
    void update(float dt);
    void reset();
    [[nodiscard]] bool has_deferred_command() const;
    void cancel_deferred_command();

private:
    struct PendingApproach {
        Command command;
        std::string hotspot_id;
    };
    struct PendingMovingApproach {
        Command command;
        std::string hotspot_id;
        sf::Vector2f last_destination;
        float elapsed = 0.0f;
    };

    [[nodiscard]] bool valid(const Command& command) const;
    [[nodiscard]] const ObjectRef* room_target(const Command& command) const;
    [[nodiscard]] VerbResult dispatch(const Command& command);
    void dispatch_and_feedback(const Command& command);
    void reject();

    RoomCommandProcessorHost& host_;
    std::optional<PendingApproach> pending_approach_;
    std::optional<PendingMovingApproach> pending_moving_approach_;
};

} // namespace pac::pnc
