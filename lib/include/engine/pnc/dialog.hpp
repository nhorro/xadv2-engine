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

/// What the dialog runtime calls back into the host (RoomScene) for: rendering
/// speech, asking whether a bubble is still on screen, and persisting `once`
/// consumption. Decoupling these keeps the runtime free of SpeechManager,
/// StateStore, and SFML — the host owns persistence so flags survive across
/// dialog sessions and save/load.
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
    enum class State { SPEAKING_NPC, AWAITING_CHOICE, SPEAKING_PLAYER, ENDED };

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
