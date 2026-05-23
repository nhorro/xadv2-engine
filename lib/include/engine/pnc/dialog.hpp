#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace pac::core {
class Diagnostics;
class ResourceCache;
class Scripting;
} // namespace pac::core

namespace pac::pnc {

/// Opaque Lua function reference passed from the dialog runtime to the host so
/// the host can spawn it as a coroutine task. Defined in the dialog TU (where
/// sol2 lives); public clients only forward it by pointer.
struct DialogRunFn;

/// What the dialog runtime calls back into the host (RoomScene) for: rendering
/// speech, asking whether a bubble is still on screen, and persisting `once`
/// consumption. Decoupling these keeps the runtime free of SpeechManager,
/// StateStore, and SFML — the host owns persistence so flags survive across
/// dialog sessions and save/load.
///
/// Run-callback hooks (`spawn_run`/`is_run_running`) let the host execute an
/// option's `run` function as a coroutine task in the dialog's scope, so that
/// blocking script APIs (`wait`, `talk`, `move_to`) inside `run` yield as
/// expected. Tests can wire these synchronously (run, immediately false) and
/// preserve the simpler semantics.
///
/// `should_end()` lets the host abort the dialog after `run` completes — used
/// to honor a queued `change_room` from within the callback.
struct DialogHost {
    std::function<void(const std::string& text)> speak_npc;
    std::function<void(const std::string& text)> speak_player;
    std::function<bool()> is_speaking;
    /// Has the option at (node_id, raw_option_index) been consumed by a prior
    /// `once`? The dialog id is captured by the host (closure) when the host
    /// is built, so the runtime never needs it.
    std::function<bool(const std::string& node_id, int option_index)> is_option_consumed;
    /// Mark (node_id, raw_option_index) as consumed.
    std::function<void(const std::string& node_id, int option_index)> mark_option_consumed;
    /// Spawn the option's `run` callback (or invoke it synchronously in tests).
    /// `fn` is owned by the runtime for the duration of the call. Optional: if
    /// unset, the runtime calls the function inline and treats it as finished
    /// immediately.
    std::function<void(DialogRunFn& fn)> spawn_run;
    /// True while the most recently spawned `run` task is still alive in the
    /// scheduler. Optional: if unset, the runtime treats `run` as synchronous.
    std::function<bool()> is_run_running;
    /// Polled after a `run` task finishes. If true, the dialog ends without
    /// following the option's `to`. Used to honor a queued `change_room`.
    std::function<bool()> should_end;
    /// Called once at start with the dialog's `text_anchor` (a room point name)
    /// if the dialog declares one, before the first NPC line is spoken, so the
    /// host can place NPC speech at a fixed point instead of the avatar. Optional.
    std::function<void(const std::string& point_name)> set_text_anchor;
};

/// One option visible at the current dialog node, after `when` filtering and
/// `once` consumption. `index` is the position in `options()`; pass it to
/// `choose`. `text` is the localized label.
struct DialogOption {
    int index = 0;
    std::string text;
};

/// Runtime for one dialog tree (per design 04 §Dialog system). The header keeps
/// sol2 out via pimpl: the dialog table is loaded inside `start()` from
/// `dialogs/<npc_id>.lua`. The runtime is a small state machine driven by
/// `update()` from RoomScene; it advances when the speech bubble clears.
/// Lifetime is room-scoped.
class DialogRuntime {
public:
    /// SPEAKING_NPC: the bubble shows an NPC line (one of possibly many for the
    /// node). AWAITING_CHOICE: options panel up. SPEAKING_PLAYER: the chosen
    /// option's line is on screen. RUNNING_CALLBACK: the option's `run` task
    /// is in flight (possibly yielding for `wait`/`talk`/`move_to`/`start_dialog`);
    /// the runtime polls the host until the task finishes, then either ends
    /// (if `should_end()` flagged a queued change_room) or follows `to`. ENDED:
    /// terminal — `on_exit` already ran.
    enum class State { SPEAKING_NPC, AWAITING_CHOICE, SPEAKING_PLAYER, RUNNING_CALLBACK, ENDED };

    /// Load `dialogs/<npc_id>.lua` and start the dialog. Calls `on_enter` then
    /// enters the `start` node before returning. Returns nullopt on a missing
    /// or malformed file (the error is logged).
    static std::optional<DialogRuntime> start(pac::core::Scripting& scripting,
                                              pac::core::ResourceCache& resources,
                                              pac::core::Diagnostics& log,
                                              const std::string& npc_id,
                                              DialogHost host);

    DialogRuntime(const DialogRuntime&) = delete;
    DialogRuntime& operator=(const DialogRuntime&) = delete;
    DialogRuntime(DialogRuntime&&) noexcept;
    DialogRuntime& operator=(DialogRuntime&&) noexcept;
    ~DialogRuntime();

    [[nodiscard]] const std::string& npc_id() const;
    [[nodiscard]] State state() const;
    [[nodiscard]] bool ended() const;
    [[nodiscard]] const std::string& current_node() const;
    /// The dialog's declared `text_anchor` (a room point name), or empty if none.
    /// Where NPC speech for this dialog should be anchored (resolved by the host).
    [[nodiscard]] const std::string& text_anchor() const;

    /// Visible options at the current node (only meaningful in AWAITING_CHOICE).
    [[nodiscard]] std::vector<DialogOption> options() const;

    /// Advance the state machine. Should be called from RoomScene::update each
    /// frame: when the speech bubble clears, the runtime moves to the next
    /// line, the option list, or the next node.
    void update();

    /// Player picked the option at `index` in `options()`. No-op outside
    /// AWAITING_CHOICE or with an out-of-range index.
    void choose(int index);

private:
    struct Impl;
    explicit DialogRuntime(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;

    friend struct DialogInternal;
};

} // namespace pac::pnc
