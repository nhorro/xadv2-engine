#include "engine/core/audio.hpp"

#include "engine/core/diagnostics.hpp"
#include "engine/core/resource_cache.hpp"
#include "engine/core/settings.hpp"

#include <SFML/Audio/SoundBuffer.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <random>

namespace pac::core {

detail::MusicCrossfadeGains detail::music_crossfade_gains(float progress) {
    constexpr float kHalfPi = 1.57079632679f;
    const float t = std::isfinite(progress) ? std::clamp(progress, 0.0f, 1.0f) : 0.0f;
    // Trigonometric round-off at pi/2 can produce a tiny negative cosine.
    // OpenAL rejects even -0.000004 as an invalid AL_GAIN, so clamp the final
    // values as well as the input progress.
    return {std::clamp(std::cos(t * kHalfPi), 0.0f, 1.0f),
            std::clamp(std::sin(t * kHalfPi), 0.0f, 1.0f)};
}

float detail::sound_fade_gain(float progress) {
    return music_crossfade_gains(progress).outgoing;
}

MusicPlayer::MusicPlayer(ResourceCache& resources, Diagnostics& log)
    : resources_(resources), log_(log) {}

bool MusicPlayer::open(int deck, const std::string& logical, bool loop) {
    // sf::Music streams from the buffer we pass in, so the buffer must outlive
    // the music (a requirement of openFromMemory). ResourceCache::persistent_bytes
    // caches the buffer with a stable address — the same path works for both the
    // loose-files and packed backends.
    const std::vector<std::byte>* bytes = nullptr;
    try {
        bytes = resources_.persistent_bytes(logical);
    } catch (const std::exception& e) {
        log_.warn(std::string("music: ") + e.what());
        return false;
    }
    if (!bytes || bytes->empty()) {
        log_.warn("music: could not load '" + logical + "'");
        return false;
    }
    if (!decks_[deck].openFromMemory(bytes->data(), bytes->size())) {
        log_.warn("music: could not decode '" + logical + "'");
        return false;
    }
    decks_[deck].setLoop(loop);
    logical_[deck] = logical;
    loops_[deck] = loop;
    return true;
}

void MusicPlayer::cancel_fade() {
    if (stopping_) {
        decks_[active_].setVolume(volume_ * deck_gains_[active_] * 100.0f);
        stopping_ = false;
    }
    if (!fading_)
        return;

    // A third request cannot keep all three streams alive with two decks. Keep
    // whichever side of the interrupted fade is currently louder so a late
    // interruption does not bring the nearly silent outgoing track back.
    const detail::MusicCrossfadeGains gains =
        detail::music_crossfade_gains(elapsed_ / fade_duration_);
    if (incoming_ >= 0 && gains.incoming > gains.outgoing) {
        decks_[active_].stop();
        active_ = incoming_;
    } else if (incoming_ >= 0) {
        decks_[incoming_].stop();
    }

    incoming_ = -1;
    fading_ = false;
    apply_volumes();
}

void MusicPlayer::play(const std::string& logical, bool loop, float gain01) {
    cancel_fade();
    const int next = 1 - active_;
    decks_[next].stop();
    if (!open(next, logical, loop))
        return;

    deck_gains_[next] = std::clamp(gain01, 0.0f, 1.0f);
    decks_[active_].stop();
    active_ = next;
    apply_volumes();
    decks_[active_].play();
}

bool MusicPlayer::crossfade(const std::string& logical,
                            float fade_seconds,
                            bool preserve_offset,
                            bool loop,
                            float gain01) {
    cancel_fade();
    const int next = 1 - active_;
    decks_[next].stop();
    if (!open(next, logical, loop))
        return false;
    deck_gains_[next] = std::clamp(gain01, 0.0f, 1.0f);

    if (preserve_offset && decks_[active_].getStatus() == sf::SoundSource::Playing) {
        const float duration = decks_[next].getDuration().asSeconds();
        if (duration > 0.0f) {
            const float offset =
                std::fmod(decks_[active_].getPlayingOffset().asSeconds(), duration);
            decks_[next].setPlayingOffset(sf::seconds(offset));
        }
    }

    decks_[next].setVolume(0.0f);
    decks_[next].play();
    incoming_ = next;
    elapsed_ = 0.0f;
    fade_duration_ = std::isfinite(fade_seconds) ? std::max(fade_seconds, 0.01f) : 2.5f;
    fading_ = true;
    apply_volumes();
    return true;
}

void MusicPlayer::fade_out(float fade_seconds) {
    if (stopping_)
        return;
    cancel_fade();
    if (decks_[active_].getStatus() != sf::SoundSource::Playing) {
        stop();
        return;
    }
    elapsed_ = 0.0f;
    fade_duration_ = std::isfinite(fade_seconds) ? std::max(fade_seconds, 0.01f) : 2.5f;
    stopping_ = true;
    apply_volumes();
}

void MusicPlayer::update(float delta_seconds) {
    if (stopping_) {
        elapsed_ += std::max(delta_seconds, 0.0f);
        const float progress = std::clamp(elapsed_ / fade_duration_, 0.0f, 1.0f);
        apply_volumes();
        if (progress >= 1.0f) {
            decks_[active_].stop();
            stopping_ = false;
        }
        return;
    }
    if (!fading_)
        return;

    elapsed_ += std::max(delta_seconds, 0.0f);
    const float progress = std::clamp(elapsed_ / fade_duration_, 0.0f, 1.0f);
    apply_volumes();
    if (progress < 1.0f)
        return;

    decks_[active_].stop();
    active_ = incoming_;
    incoming_ = -1;
    fading_ = false;
    apply_volumes();
}

void MusicPlayer::apply_volumes() {
    if (stopping_) {
        const float gain = detail::music_crossfade_gains(elapsed_ / fade_duration_).outgoing;
        decks_[active_].setVolume(volume_ * deck_gains_[active_] * gain * 100.0f);
        return;
    }
    if (!fading_ || incoming_ < 0) {
        decks_[active_].setVolume(volume_ * deck_gains_[active_] * 100.0f);
        return;
    }
    const detail::MusicCrossfadeGains gains =
        detail::music_crossfade_gains(elapsed_ / fade_duration_);
    decks_[active_].setVolume(volume_ * deck_gains_[active_] * gains.outgoing * 100.0f);
    decks_[incoming_].setVolume(volume_ * deck_gains_[incoming_] * gains.incoming * 100.0f);
}

void MusicPlayer::stop() {
    for (sf::Music& deck : decks_)
        deck.stop();
    incoming_ = -1;
    fading_ = false;
    stopping_ = false;
}

float MusicPlayer::playing_offset_seconds() const {
    const sf::Music& current = incoming_ >= 0 ? decks_[incoming_] : decks_[active_];
    if (current.getStatus() != sf::SoundSource::Playing) {
        return 0.0f;
    }
    return current.getPlayingOffset().asSeconds();
}

bool MusicPlayer::is_playing() const {
    const sf::Music& current = incoming_ >= 0 ? decks_[incoming_] : decks_[active_];
    return current.getStatus() == sf::SoundSource::Playing;
}

MusicState MusicPlayer::capture_state() const {
    const int current = incoming_ >= 0 ? incoming_ : active_;
    MusicState state;
    state.playing = decks_[current].getStatus() == sf::SoundSource::Playing;
    state.logical = logical_[current];
    state.loop = loops_[current];
    state.gain = deck_gains_[current];
    if (state.playing) {
        state.offset_seconds = decks_[current].getPlayingOffset().asSeconds();
    }
    return state;
}

bool MusicPlayer::restore_state(const MusicState& state, float fade_seconds) {
    if (!state.playing || state.logical.empty()) {
        // If the temporary cue is still fading in from silence, turn that same
        // envelope around. This preserves its current audible gain instead of
        // briefly jumping to full volume before the fade-out begins.
        if (fading_ && incoming_ >= 0 && decks_[active_].getStatus() != sf::SoundSource::Playing) {
            decks_[active_].stop();
            active_ = incoming_;
            incoming_ = -1;
            elapsed_ = std::max(fade_duration_ - elapsed_, 0.0f);
            fading_ = false;
            stopping_ = true;
            apply_volumes();
            return true;
        }
        fade_out(fade_seconds);
        return true;
    }

    // While the temporary cue is still entering, the old cue remains on the
    // outgoing deck. Reverse the equal-power envelope in place: both volumes
    // remain continuous, even when a close-up is opened and closed quickly.
    if (fading_ && incoming_ >= 0 && logical_[active_] == state.logical &&
        decks_[active_].getStatus() == sf::SoundSource::Playing) {
        std::swap(active_, incoming_);
        elapsed_ = std::max(fade_duration_ - elapsed_, 0.0f);
        deck_gains_[incoming_] = std::clamp(state.gain, 0.0f, 1.0f);
        decks_[incoming_].setLoop(state.loop);
        loops_[incoming_] = state.loop;
        apply_volumes();
        return true;
    }

    cancel_fade();
    // If the temporary cue was closed before its fade-in took over, cancellation
    // may already have retained the previous deck. Avoid crossfading a track into
    // a second copy of itself (which can phase audibly).
    if (logical_[active_] == state.logical &&
        decks_[active_].getStatus() == sf::SoundSource::Playing) {
        deck_gains_[active_] = std::clamp(state.gain, 0.0f, 1.0f);
        decks_[active_].setLoop(state.loop);
        loops_[active_] = state.loop;
        apply_volumes();
        return true;
    }
    const int next = 1 - active_;
    decks_[next].stop();
    if (!open(next, state.logical, state.loop)) {
        // The temporary score must not become permanent just because a prior
        // cue can no longer be loaded. Leave ambience untouched and fade the
        // temporary music away as the safest fallback.
        fade_out(fade_seconds);
        return false;
    }
    deck_gains_[next] = std::clamp(state.gain, 0.0f, 1.0f);
    const float duration = decks_[next].getDuration().asSeconds();
    if (duration > 0.0f && state.offset_seconds > 0.0f) {
        const float offset = state.loop ? std::fmod(state.offset_seconds, duration)
                                        : std::min(state.offset_seconds, duration);
        decks_[next].setPlayingOffset(sf::seconds(offset));
    }

    decks_[next].setVolume(0.0f);
    decks_[next].play();
    incoming_ = next;
    elapsed_ = 0.0f;
    fade_duration_ = std::isfinite(fade_seconds) ? std::max(fade_seconds, 0.01f) : 2.5f;
    fading_ = true;
    apply_volumes();
    return true;
}

void MusicPlayer::set_volume(float volume01) {
    volume_ = std::clamp(volume01, 0.0f, 1.0f);
    apply_volumes();
}

SoundPlayer::SoundPlayer(ResourceCache& resources, Diagnostics& log)
    : resources_(resources), log_(log) {}

void SoundPlayer::play(const std::string& logical, float volume01, float pan) {
    const sf::SoundBuffer* buffer = nullptr;
    try {
        buffer = &resources_.sound_buffer(logical);
    } catch (const std::exception& e) {
        log_.warn(std::string("sound: ") + e.what());
        return;
    }

    std::size_t voice_index = voices_.size();
    for (std::size_t i = 0; i < voices_.size(); ++i) {
        if (voices_[i].sound.getStatus() == sf::Sound::Stopped) {
            voice_index = i;
            break;
        }
    }
    if (voice_index == voices_.size()) {
        if (voices_.size() >= kMaxVoices) {
            return; // all voices busy; drop this one
        }
        voices_.emplace_back();
    }
    Voice& voice = voices_[voice_index];
    voice.logical = logical;
    voice.gain = std::clamp(volume01, 0.0f, 1.0f);
    voice.fade_start = 1.0f;
    voice.fade_elapsed = 0.0f;
    voice.fade_duration = 0.0f;
    voice.fading = false;
    voice.sound.setBuffer(*buffer);
    apply_voice_volume(voice);

    // Stereo balance: place the (mono) source on a forward arc around the
    // listener so `pan` maps proportionally to L/R, with distance attenuation off
    // so only direction matters. pan=0 sits dead ahead (centered, full volume).
    pan = std::clamp(pan, -1.0f, 1.0f);
    const float theta = pan * 1.5707963f; // ±90° at the extremes
    voice.sound.setRelativeToListener(true);
    voice.sound.setMinDistance(1.0f);
    voice.sound.setAttenuation(0.0f);
    voice.sound.setPosition(std::sin(theta), 0.0f, -std::cos(theta));

    voice.sound.play();
}

void SoundPlayer::stop_voice(Voice& voice, float fade_seconds) {
    if (voice.sound.getStatus() == sf::Sound::Stopped) {
        voice.fading = false;
        return;
    }
    if (fade_seconds <= 0.0f) {
        voice.sound.stop();
        voice.fading = false;
        return;
    }

    const float current_fade =
        voice.fading
            ? voice.fade_start * detail::sound_fade_gain(voice.fade_elapsed / voice.fade_duration)
            : 1.0f;
    voice.fade_start = current_fade;
    voice.fade_elapsed = 0.0f;
    voice.fade_duration = fade_seconds;
    voice.fading = true;
    apply_voice_volume(voice);
}

void SoundPlayer::stop(const std::string& logical, float fade_seconds) {
    fade_seconds = std::isfinite(fade_seconds) ? std::max(fade_seconds, 0.0f) : 0.0f;
    for (Voice& voice : voices_) {
        if (voice.logical == logical) {
            stop_voice(voice, fade_seconds);
        }
    }
}

void SoundPlayer::stop_all(float fade_seconds) {
    fade_seconds = std::isfinite(fade_seconds) ? std::max(fade_seconds, 0.0f) : 0.0f;
    for (Voice& voice : voices_) {
        stop_voice(voice, fade_seconds);
    }
}

void SoundPlayer::update(float delta_seconds) {
    delta_seconds = std::max(delta_seconds, 0.0f);
    for (Voice& voice : voices_) {
        if (!voice.fading) {
            continue;
        }
        voice.fade_elapsed += delta_seconds;
        apply_voice_volume(voice);
        if (voice.fade_elapsed >= voice.fade_duration) {
            voice.sound.stop();
            voice.fading = false;
        }
    }
}

void SoundPlayer::apply_voice_volume(Voice& voice) {
    const float fade =
        voice.fading
            ? voice.fade_start * detail::sound_fade_gain(voice.fade_elapsed / voice.fade_duration)
            : 1.0f;
    voice.sound.setVolume(volume_ * voice.gain * fade * 100.0f);
}

void SoundPlayer::set_volume(float volume01) {
    volume_ = std::clamp(volume01, 0.0f, 1.0f);
    for (Voice& voice : voices_) {
        apply_voice_volume(voice);
    }
}

VoicePlayer::VoicePlayer(ResourceCache& resources, Diagnostics& log)
    : resources_(resources), log_(log) {}

std::optional<float> VoicePlayer::play(const std::string& logical) {
    stop();
    if (!enabled_ || logical.empty()) {
        return std::nullopt;
    }
    try {
        const sf::SoundBuffer& buffer = resources_.sound_buffer(logical);
        sound_.setBuffer(buffer);
        sound_.setRelativeToListener(true);
        sound_.setPosition(0.0f, 0.0f, -1.0f);
        sound_.setAttenuation(0.0f);
        apply_volume();
        sound_.play();
        return buffer.getDuration().asSeconds();
    } catch (const std::exception& e) {
        log_.warn(std::string("voice: ") + e.what());
        return std::nullopt;
    }
}

void VoicePlayer::stop() {
    sound_.stop();
}

void VoicePlayer::set_enabled(bool enabled) {
    enabled_ = enabled;
    if (!enabled_) {
        stop();
    }
}

void VoicePlayer::set_volume(float volume01) {
    volume_ = std::clamp(volume01, 0.0f, 1.0f);
    apply_volume();
}

bool VoicePlayer::is_playing() const {
    return sound_.getStatus() == sf::SoundSource::Playing;
}

void VoicePlayer::apply_volume() {
    sound_.setVolume(volume_ * 100.0f);
}

AmbiencePlayer::AmbiencePlayer(ResourceCache& resources, Diagnostics& log, SoundPlayer& sfx)
    : resources_(resources), log_(log), sfx_(sfx), random_(std::random_device{}()) {}

bool AmbiencePlayer::open(int deck, const std::string& logical) {
    const std::vector<std::byte>* bytes = nullptr;
    try {
        bytes = resources_.persistent_bytes(logical);
    } catch (const std::exception& e) {
        log_.warn(std::string("ambience: ") + e.what());
        return false;
    }
    if (!bytes || bytes->empty()) {
        log_.warn("ambience: could not load '" + logical + "'");
        return false;
    }
    if (!decks_[deck].openFromMemory(bytes->data(), bytes->size())) {
        log_.warn("ambience: could not decode '" + logical + "'");
        return false;
    }
    decks_[deck].setLoop(true);
    logical_[deck] = logical;
    return true;
}

void AmbiencePlayer::cancel_transition() {
    if (transition_ == Transition::CROSSFADE && incoming_ >= 0) {
        const float progress = transition_duration_ > 0.0f
                                   ? std::clamp(elapsed_ / transition_duration_, 0.0f, 1.0f)
                                   : 1.0f;
        const detail::MusicCrossfadeGains gains = detail::music_crossfade_gains(progress);
        const float outgoing_gain = start_gain_ * gains.outgoing;
        const float incoming_gain = target_gain_ * gains.incoming;
        if (incoming_gain > outgoing_gain) {
            decks_[active_].stop();
            logical_[active_].clear();
            active_ = incoming_;
            current_gain_ = incoming_gain;
        } else {
            decks_[incoming_].stop();
            logical_[incoming_].clear();
            current_gain_ = outgoing_gain;
        }
        incoming_ = -1;
    }
    transition_ = Transition::NONE;
    apply_loop_volumes();
}

void AmbiencePlayer::set_base(const std::string& logical,
                              float volume01,
                              float transition_seconds) {
    volume01 = std::clamp(volume01, 0.0f, 1.0f);
    transition_seconds =
        std::isfinite(transition_seconds) ? std::max(transition_seconds, 0.0f) : 2.5f;
    cancel_transition();

    const bool have_active =
        !logical_[active_].empty() && decks_[active_].getStatus() == sf::SoundSource::Playing;
    if (logical.empty()) {
        if (!have_active) {
            current_gain_ = 0.0f;
            return;
        }
        start_gain_ = current_gain_;
        target_gain_ = 0.0f;
        elapsed_ = 0.0f;
        transition_duration_ = transition_seconds;
        transition_ = Transition::STOP;
        update_loop(0.0f);
        return;
    }

    if (have_active && logical_[active_] == logical) {
        // This is the continuity seam: do not reopen or seek an identical loop.
        start_gain_ = current_gain_;
        target_gain_ = volume01;
        elapsed_ = 0.0f;
        transition_duration_ = transition_seconds;
        transition_ = Transition::VOLUME;
        update_loop(0.0f);
        return;
    }

    const int next = 1 - active_;
    decks_[next].stop();
    logical_[next].clear();
    if (!open(next, logical)) {
        return; // keep the old ambience when the replacement is unusable
    }
    decks_[next].setVolume(0.0f);
    decks_[next].play();

    start_gain_ = have_active ? current_gain_ : 0.0f;
    target_gain_ = volume01;
    elapsed_ = 0.0f;
    transition_duration_ = transition_seconds;
    if (have_active) {
        incoming_ = next;
        transition_ = Transition::CROSSFADE;
    } else {
        active_ = next;
        current_gain_ = 0.0f;
        transition_ = Transition::VOLUME;
    }
    update_loop(0.0f);
}

void AmbiencePlayer::set_base_volume(float volume01, float transition_seconds) {
    const std::string logical = base_sound();
    if (logical.empty()) {
        return;
    }
    set_base(logical, volume01, transition_seconds);
}

void AmbiencePlayer::configure(const AmbienceDefinition& definition) {
    set_base(definition.base.sound, definition.base.volume, definition.transition);

    // Preserve a timer and any Lua overrides when the same named random layer is
    // shared by adjacent rooms; room-specific ids naturally start fresh.
    std::map<std::string, RuntimeRandomLayer> previous;
    for (RuntimeRandomLayer& layer : random_layers_) {
        previous.emplace(layer.definition.id, std::move(layer));
    }
    random_layers_.clear();
    random_layers_.reserve(definition.random.size());
    for (const AmbienceRandomLayer& authored : definition.random) {
        RuntimeRandomLayer runtime;
        runtime.definition = authored;
        if (const auto it = previous.find(authored.id); it != previous.end()) {
            runtime.remaining = it->second.remaining;
            runtime.volume_scale = it->second.volume_scale;
            runtime.enabled = it->second.enabled;
        } else {
            runtime.remaining = next_delay(authored);
        }
        random_layers_.push_back(std::move(runtime));
    }
}

void AmbiencePlayer::stop(float transition_seconds) {
    random_layers_.clear();
    set_base({}, 0.0f, transition_seconds);
}

float AmbiencePlayer::random_in(AmbienceRange range) {
    if (range.max < range.min) {
        std::swap(range.min, range.max);
    }
    return std::uniform_real_distribution<float>(range.min, range.max)(random_);
}

float AmbiencePlayer::next_delay(const AmbienceRandomLayer& layer) {
    return std::max(random_in(layer.delay), 0.01f);
}

void AmbiencePlayer::update(float delta_seconds) {
    delta_seconds = std::max(delta_seconds, 0.0f);
    update_loop(delta_seconds);

    for (RuntimeRandomLayer& runtime : random_layers_) {
        if (!runtime.enabled || runtime.definition.sounds.empty()) {
            continue;
        }
        runtime.remaining -= delta_seconds;
        if (runtime.remaining > 0.0f) {
            continue;
        }
        const std::size_t index =
            std::uniform_int_distribution<std::size_t>(0, runtime.definition.sounds.size() - 1)(
                random_);
        const float volume = random_in(runtime.definition.volume) * runtime.volume_scale;
        const float pan = random_in(runtime.definition.pan);
        sfx_.play(runtime.definition.sounds[index], volume, pan);
        runtime.remaining = next_delay(runtime.definition);
    }
}

void AmbiencePlayer::update_loop(float delta_seconds) {
    if (transition_ == Transition::NONE) {
        apply_loop_volumes();
        return;
    }
    elapsed_ += delta_seconds;
    const float progress = transition_duration_ > 0.0f
                               ? std::clamp(elapsed_ / transition_duration_, 0.0f, 1.0f)
                               : 1.0f;

    if (transition_ == Transition::CROSSFADE) {
        apply_loop_volumes();
        if (progress >= 1.0f && incoming_ >= 0) {
            decks_[active_].stop();
            logical_[active_].clear();
            active_ = incoming_;
            incoming_ = -1;
            current_gain_ = target_gain_;
            transition_ = Transition::NONE;
            apply_loop_volumes();
        }
        return;
    }

    current_gain_ = start_gain_ + (target_gain_ - start_gain_) * progress;
    apply_loop_volumes();
    if (progress < 1.0f) {
        return;
    }
    if (transition_ == Transition::STOP) {
        decks_[active_].stop();
        logical_[active_].clear();
        current_gain_ = 0.0f;
    }
    transition_ = Transition::NONE;
}

void AmbiencePlayer::apply_loop_volumes() {
    if (transition_ == Transition::CROSSFADE && incoming_ >= 0) {
        const float progress = transition_duration_ > 0.0f
                                   ? std::clamp(elapsed_ / transition_duration_, 0.0f, 1.0f)
                                   : 1.0f;
        const detail::MusicCrossfadeGains gains = detail::music_crossfade_gains(progress);
        decks_[active_].setVolume(global_volume_ * start_gain_ * gains.outgoing * 100.0f);
        decks_[incoming_].setVolume(global_volume_ * target_gain_ * gains.incoming * 100.0f);
        return;
    }
    decks_[active_].setVolume(global_volume_ * current_gain_ * 100.0f);
}

void AmbiencePlayer::set_volume(float volume01) {
    global_volume_ = std::clamp(volume01, 0.0f, 1.0f);
    apply_loop_volumes();
}

bool AmbiencePlayer::set_random_layer_enabled(const std::string& id, bool enabled) {
    for (RuntimeRandomLayer& layer : random_layers_) {
        if (layer.definition.id == id) {
            layer.enabled = enabled;
            return true;
        }
    }
    return false;
}

bool AmbiencePlayer::set_random_layer_volume(const std::string& id, float volume01) {
    for (RuntimeRandomLayer& layer : random_layers_) {
        if (layer.definition.id == id) {
            layer.volume_scale = std::clamp(volume01, 0.0f, 1.0f);
            return true;
        }
    }
    return false;
}

const std::string& AmbiencePlayer::base_sound() const {
    return incoming_ >= 0 ? logical_[incoming_] : logical_[active_];
}

AudioServices::AudioServices(ResourceCache& resources, Diagnostics& log, const Settings& settings)
    : music(resources, log), sfx(resources, log), ambience(resources, log, sfx),
      voice(resources, log) {
    apply_settings(settings);
}

void AudioServices::apply_settings(const Settings& settings) {
    music.set_volume(settings.audio.music_volume);
    sfx.set_volume(settings.audio.sfx_volume);
    ambience.set_volume(settings.audio.sfx_volume);
    voice.set_volume(settings.audio.sfx_volume);
    voice.set_enabled(settings.audio.speech_enabled);
}

} // namespace pac::core
