#include "engine/core/audio.hpp"

#include "engine/core/diagnostics.hpp"
#include "engine/core/resource_cache.hpp"
#include "engine/core/settings.hpp"

#include <SFML/Audio/SoundBuffer.hpp>

#include <algorithm>
#include <cmath>

namespace pac::core {

MusicPlayer::MusicPlayer(ResourceCache& resources, Diagnostics& log)
    : resources_(resources), log_(log) {}

void MusicPlayer::play(const std::string& logical, bool loop) {
    // sf::Music streams from the buffer we pass in, so the buffer must outlive
    // the music (a requirement of openFromMemory). ResourceCache::persistent_bytes
    // caches the buffer with a stable address — the same path works for both the
    // loose-files and packed backends.
    const std::vector<std::byte>* bytes = nullptr;
    try {
        bytes = resources_.persistent_bytes(logical);
    } catch (const std::exception& e) {
        log_.warn(std::string("music: ") + e.what());
        return;
    }
    if (!bytes || bytes->empty()) {
        log_.warn("music: could not load '" + logical + "'");
        return;
    }
    if (!music_.openFromMemory(bytes->data(), bytes->size())) {
        log_.warn("music: could not decode '" + logical + "'");
        return;
    }
    music_.setLoop(loop);
    music_.setVolume(volume_ * 100.0f);
    music_.play();
}

void MusicPlayer::stop() {
    music_.stop();
}

float MusicPlayer::playing_offset_seconds() const {
    if (music_.getStatus() != sf::SoundSource::Playing) {
        return 0.0f;
    }
    return music_.getPlayingOffset().asSeconds();
}

bool MusicPlayer::is_playing() const {
    return music_.getStatus() == sf::SoundSource::Playing;
}

void MusicPlayer::set_volume(float volume01) {
    volume_ = volume01;
    music_.setVolume(volume_ * 100.0f);
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
