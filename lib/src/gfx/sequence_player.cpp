#include "engine/gfx/sequence_player.hpp"

#include <utility>

namespace pac::gfx {

SequencePlayer::SequencePlayer(Animation anim) : anim_(std::move(anim)) {}

void SequencePlayer::play(const std::string& sequence, bool restart) {
    if (current_ == sequence && !restart) {
        return;
    }
    current_ = sequence;
    frame_index_ = 0;
    elapsed_ = 0.0f;
    finished_ = false;
}

void SequencePlayer::update(float dt) {
    if (finished_) {
        return;
    }
    const Sequence* seq = anim_.sequence(current_);
    if (!seq || seq->frames.empty()) {
        return;
    }

    elapsed_ += dt;
    // Advance across as many frames as the elapsed time covers. A non-positive
    // duration is treated as a single tick to avoid an infinite loop.
    while (true) {
        const float dur = seq->frames[frame_index_].duration;
        if (dur > 0.0f && elapsed_ < dur) {
            break;
        }
        elapsed_ -= (dur > 0.0f ? dur : elapsed_);
        if (frame_index_ + 1 < seq->frames.size()) {
            ++frame_index_;
        } else if (seq->loop) {
            frame_index_ = 0;
        } else {
            finished_ = true;
            if (on_finished_) {
                on_finished_();
            }
            break;
        }
    }
}

std::string SequencePlayer::current_frame_id() const {
    const Sequence* seq = anim_.sequence(current_);
    if (!seq || frame_index_ >= seq->frames.size()) {
        return {};
    }
    return seq->frames[frame_index_].sprite;
}

} // namespace pac::gfx
