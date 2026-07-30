#include "engine/core/audio.hpp"

#include "engine/core/diagnostics.hpp"
#include "engine/core/resource_cache.hpp"
#include "engine/core/settings.hpp"

#include <SFML/Audio/SoundBuffer.hpp>

#include <algorithm>
#include <cmath>

namespace pac::core {

detail::MusicCrossfadeGains detail::music_crossfade_gains(float progress) {
    constexpr float kHalfPi = 1.57079632679f;
    const float t = std::clamp(progress, 0.0f, 1.0f);
    return {std::cos(t * kHalfPi), std::sin(t * kHalfPi)};
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
    return true;
}

void MusicPlayer::cancel_fade() {
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

    decks_[active_].setVolume(volume_ * 100.0f);
    incoming_ = -1;
    fading_ = false;
}

void MusicPlayer::play(const std::string& logical, bool loop) {
    cancel_fade();
    const int next = 1 - active_;
    decks_[next].stop();
    if (!open(next, logical, loop))
        return;

    decks_[active_].stop();
    active_ = next;
    decks_[active_].setVolume(volume_ * 100.0f);
    decks_[active_].play();
}

bool MusicPlayer::crossfade(const std::string& logical,
                            float fade_seconds,
                            bool preserve_offset,
                            bool loop) {
    cancel_fade();
    const int next = 1 - active_;
    decks_[next].stop();
    if (!open(next, logical, loop))
        return false;

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

void MusicPlayer::update(float delta_seconds) {
    if (!fading_)
        return;

    elapsed_ += std::max(delta_seconds, 0.0f);
    const float progress = std::clamp(elapsed_ / fade_duration_, 0.0f, 1.0f);
    apply_volumes();
    if (progress < 1.0f)
        return;

    decks_[active_].stop();
    decks_[active_].setVolume(volume_ * 100.0f);
    active_ = incoming_;
    incoming_ = -1;
    fading_ = false;
    decks_[active_].setVolume(volume_ * 100.0f);
}

void MusicPlayer::apply_volumes() {
    if (!fading_ || incoming_ < 0) {
        decks_[active_].setVolume(volume_ * 100.0f);
        return;
    }
    const detail::MusicCrossfadeGains gains =
        detail::music_crossfade_gains(elapsed_ / fade_duration_);
    decks_[active_].setVolume(volume_ * gains.outgoing * 100.0f);
    decks_[incoming_].setVolume(volume_ * gains.incoming * 100.0f);
}

void MusicPlayer::stop() {
    for (sf::Music& deck : decks_)
        deck.stop();
    incoming_ = -1;
    fading_ = false;
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

    sf::Sound* voice = nullptr;
    for (sf::Sound& s : voices_) {
        if (s.getStatus() == sf::Sound::Stopped) {
            voice = &s;
            break;
        }
    }
    if (!voice) {
        if (voices_.size() >= kMaxVoices) {
            return; // all voices busy; drop this one
        }
        voice = &voices_.emplace_back();
    }
    voice->setBuffer(*buffer);
    voice->setVolume(std::clamp(volume_ * volume01, 0.0f, 1.0f) * 100.0f);

    // Stereo balance: place the (mono) source on a forward arc around the
    // listener so `pan` maps proportionally to L/R, with distance attenuation off
    // so only direction matters. pan=0 sits dead ahead (centered, full volume).
    pan = std::clamp(pan, -1.0f, 1.0f);
    const float theta = pan * 1.5707963f; // ±90° at the extremes
    voice->setRelativeToListener(true);
    voice->setMinDistance(1.0f);
    voice->setAttenuation(0.0f);
    voice->setPosition(std::sin(theta), 0.0f, -std::cos(theta));

    voice->play();
}

void SoundPlayer::stop_all() {
    for (sf::Sound& s : voices_) {
        s.stop();
    }
}

void SoundPlayer::set_volume(float volume01) {
    volume_ = volume01;
    for (sf::Sound& s : voices_) {
        s.setVolume(volume_ * 100.0f);
    }
}

AudioServices::AudioServices(ResourceCache& resources, Diagnostics& log, const Settings& settings)
    : music(resources, log), sfx(resources, log) {
    apply_settings(settings);
}

void AudioServices::apply_settings(const Settings& settings) {
    music.set_volume(settings.audio.music_volume);
    sfx.set_volume(settings.audio.sfx_volume);
}

} // namespace pac::core
