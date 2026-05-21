#pragma once

#include "engine/gfx/animation.hpp"

#include <cstddef>
#include <functional>
#include <string>

namespace pac::gfx {

/// Drives the current frame of a sequence over time. Pure timing logic with no
/// graphics context, so animation playback is headless-testable. Owns a copy of
/// the Animation so it has no dangling references.
class SequencePlayer {
public:
    explicit SequencePlayer(Animation anim);

    /// Select a sequence. `restart` rewinds even if it is already current.
    void play(const std::string& sequence, bool restart = true);
    void update(float dt);

    const std::string& current_sequence() const { return current_; }
    std::string current_frame_id() const; // "" when no valid frame
    bool finished() const { return finished_; }
    void set_on_finished(std::function<void()> callback) { on_finished_ = std::move(callback); }

private:
    Animation anim_;
    std::string current_;
    std::size_t frame_index_ = 0;
    float elapsed_ = 0.0f;
    bool finished_ = false;
    std::function<void()> on_finished_;
};

} // namespace pac::gfx
